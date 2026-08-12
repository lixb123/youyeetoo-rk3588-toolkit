#include "telemetry_worker.h"

#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "worker_channel.h"
#include "worker_supervisor.h"

namespace youyeetoo {

// ─── Telemetry collection helpers ────────────────────────────────────────────

namespace {

// Read CPU usage from /proc/stat (two samples 200 ms apart).
double ReadCpuUsagePct() {
    auto read_stat = []() -> std::pair<uint64_t, uint64_t> {
        std::ifstream f("/proc/stat");
        std::string cpu;
        uint64_t user, nice, sys, idle, iowait, irq, softirq, steal;
        if (!(f >> cpu >> user >> nice >> sys >> idle >> iowait >> irq >> softirq >> steal))
            return {0, 0};
        const uint64_t total = user + nice + sys + idle + iowait + irq + softirq + steal;
        const uint64_t used  = total - idle - iowait;
        return {used, total};
    };

    const auto [u1, t1] = read_stat();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto [u2, t2] = read_stat();

    const uint64_t du = u2 - u1;
    const uint64_t dt = t2 - t1;
    return (dt > 0) ? (100.0 * static_cast<double>(du) / static_cast<double>(dt)) : 0.0;
}

// Read /proc/meminfo for MemTotal and MemAvailable (kB).
void ReadMemInfo(uint64_t* total_kb, uint64_t* used_kb) {
    *total_kb = *used_kb = 0;
    std::ifstream f("/proc/meminfo");
    std::string key;
    uint64_t val;
    std::string unit;
    uint64_t available_kb = 0;
    while (f >> key >> val) {
        f >> unit;  // "kB"
        if (key == "MemTotal:")     *total_kb     = val;
        if (key == "MemAvailable:") available_kb  = val;
    }
    *used_kb = (*total_kb > available_kb) ? *total_kb - available_kb : 0;
}

// Read thermal zone temperature (milli-Celsius → Celsius).
double ReadThermalTemp(const std::string& zone_path) {
    std::ifstream f(zone_path);
    int milli = 0;
    if (f >> milli) return milli / 1000.0;
    return 0.0;
}

// Read disk usage via statvfs.
void ReadDiskUsage(const std::string& path, uint64_t* used_kb, uint64_t* total_kb) {
    *used_kb = *total_kb = 0;
    struct statvfs sv{};
    if (::statvfs(path.c_str(), &sv) < 0) return;
    const uint64_t block_kb = sv.f_bsize / 1024;
    *total_kb = sv.f_blocks * block_kb;
    *used_kb  = (*total_kb > sv.f_bavail * block_kb)
                ? *total_kb - sv.f_bavail * block_kb : 0;
}

}  // namespace

// ─── Child process ────────────────────────────────────────────────────────────

void TelemetryWorkerChildMain(int child_fd, const CameraAppOptions& options) {
    (void)options;
    ::signal(SIGPIPE, SIG_IGN);

    std::cout << "[telemetry_worker] starting pid=" << ::getpid() << "\n";

    WorkerChannel channel(child_fd);

    using Clock = std::chrono::steady_clock;
    using Ms    = std::chrono::milliseconds;
    const auto kInterval = Ms(2000);
    auto last_send = Clock::now() - kInterval;  // send immediately on first tick

    while (true) {
        // Check for channel errors
        std::string msg;
        bool chan_err = false;
        channel.TryRecv(&msg, &chan_err);  // drain any parent messages (e.g. config updates)
        if (chan_err) {
            std::cerr << "[telemetry_worker] channel broken, exiting\n";
            break;
        }

        const auto now = Clock::now();
        if (now - last_send >= kInterval) {
            // Collect telemetry (CPU sample blocks for 200 ms internally)
            const double cpu   = ReadCpuUsagePct();
            const double temp0 = ReadThermalTemp("/sys/class/thermal/thermal_zone0/temp");
            const double temp1 = ReadThermalTemp("/sys/class/thermal/thermal_zone1/temp");

            uint64_t mem_total = 0, mem_used = 0;
            ReadMemInfo(&mem_total, &mem_used);

            uint64_t disk_used = 0, disk_total = 0;
            ReadDiskUsage("/data", &disk_used, &disk_total);

            std::ostringstream oss;
            oss << "type=telemetry\n"
                << "cpu_usage_pct=" << cpu << '\n'
                << "mem_used_kb="   << mem_used  << '\n'
                << "mem_total_kb="  << mem_total << '\n'
                << "cpu_temp_c="    << temp0  << '\n'
                << "board_temp_c="  << temp1  << '\n'
                << "disk_used_kb="  << disk_used  << '\n'
                << "disk_total_kb=" << disk_total << '\n';

            if (!channel.Send(oss.str())) {
                std::cerr << "[telemetry_worker] send failed, exiting\n";
                break;
            }
            last_send = now;
        }

        std::this_thread::sleep_for(Ms(100));
    }

    std::cout << "[telemetry_worker] exiting pid=" << ::getpid() << "\n";
}

// ─── Parent-side proxy ────────────────────────────────────────────────────────

TelemetryWorkerProxy::TelemetryWorkerProxy(WorkerSupervisor* supervisor, std::string worker_name)
    : supervisor_(supervisor), worker_name_(std::move(worker_name)) {}

void TelemetryWorkerProxy::Tick() {
    if (supervisor_ == nullptr) return;

    const int fd = supervisor_->GetParentFd(worker_name_);
    if (fd < 0) {
        channel_alive_ = false;
        return;
    }

    if (fd != last_seen_fd_) {
        last_seen_fd_  = fd;
        channel_alive_ = true;
        status_summary_ = "telemetry=subprocess_starting";
        telemetry_ = {};
    }

    char buf[kWorkerMaxMessageSize];
    while (true) {
        struct pollfd pfd{};
        pfd.fd     = fd;
        pfd.events = POLLIN;
        if (::poll(&pfd, 1, 0) <= 0) break;

        if (pfd.revents & (POLLHUP | POLLERR)) {
            channel_alive_ = false;
            break;
        }
        const ssize_t n = ::recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n <= 0) {
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
                channel_alive_ = false;
            break;
        }
        ProcessMessage(std::string(buf, static_cast<size_t>(n)));
    }
}

void TelemetryWorkerProxy::ProcessMessage(const std::string& msg) {
    // Quick line-by-line parse for telemetry fields
    const auto getval = [&](const std::string& key) -> std::string {
        const std::string needle = key + "=";
        auto pos = msg.find(needle);
        if (pos == std::string::npos) return {};
        pos += needle.size();
        const auto end = msg.find('\n', pos);
        return msg.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    };
    const auto getd = [&](const std::string& k) -> double {
        const auto v = getval(k);
        if (v.empty()) return 0.0;
        try { return std::stod(v); } catch (...) { return 0.0; }
    };
    const auto getu = [&](const std::string& k) -> uint64_t {
        const auto v = getval(k);
        if (v.empty()) return 0;
        try { return std::stoull(v); } catch (...) { return 0; }
    };

    if (msg.find("type=telemetry") == std::string::npos) return;

    telemetry_.cpu_usage_pct  = getd("cpu_usage_pct");
    telemetry_.mem_used_kb    = getu("mem_used_kb");
    telemetry_.mem_total_kb   = getu("mem_total_kb");
    telemetry_.cpu_temp_c     = getd("cpu_temp_c");
    telemetry_.board_temp_c   = getd("board_temp_c");
    telemetry_.disk_used_kb   = getu("disk_used_kb");
    telemetry_.disk_total_kb  = getu("disk_total_kb");

    std::ostringstream oss;
    oss << "telemetry=ok"
        << " cpu=" << static_cast<int>(telemetry_.cpu_usage_pct) << "%"
        << " mem=" << static_cast<int>(telemetry_.mem_usage_pct()) << "%"
        << " cpu_temp=" << static_cast<int>(telemetry_.cpu_temp_c) << "C"
        << " disk_used=" << telemetry_.disk_used_kb / 1024 << "MB";
    status_summary_ = oss.str();
}

}  // namespace youyeetoo
