#include "camera_worker.h"

#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>

#include "camera_manager.h"
#include "worker_channel.h"
#include "worker_supervisor.h"

namespace youyeetoo {

// ─── KV serialization helpers ────────────────────────────────────────────────
// Each message is a flat set of "KEY=VALUE\n" lines.
// Internal newlines/backslashes are escaped so the entire message fits one SEQPACKET datagram.

namespace {

std::string KvEncode(const std::string& v) {
    std::string out;
    out.reserve(v.size());
    for (const char c : v) {
        if      (c == '\\') { out += "\\\\"; }
        else if (c == '\n') { out += "\\n";  }
        else                { out += c;       }
    }
    return out;
}

std::string KvDecode(const std::string& v) {
    std::string out;
    out.reserve(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            ++i;
            if      (v[i] == 'n')  { out += '\n'; }
            else if (v[i] == '\\') { out += '\\'; }
            else                   { out += v[i]; }
        } else {
            out += v[i];
        }
    }
    return out;
}

// Parse a complete message string into a key→value map.
std::map<std::string, std::string> ParseKV(const std::string& msg) {
    std::map<std::string, std::string> kv;
    std::istringstream ss(msg);
    std::string line;
    while (std::getline(ss, line)) {
        const auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        kv[line.substr(0, pos)] = KvDecode(line.substr(pos + 1));
    }
    return kv;
}

// Append one KV pair to an output stream.
void AppendKV(std::ostringstream& oss, const std::string& k, const std::string& v) {
    oss << k << '=' << KvEncode(v) << '\n';
}
void AppendKV(std::ostringstream& oss, const std::string& k, const char* v) {
    AppendKV(oss, k, std::string(v != nullptr ? v : ""));
}
void AppendKV(std::ostringstream& oss, const std::string& k, int v) {
    oss << k << '=' << v << '\n';
}
void AppendKV(std::ostringstream& oss, const std::string& k, int64_t v) {
    oss << k << '=' << v << '\n';
}
void AppendKV(std::ostringstream& oss, const std::string& k, uint64_t v) {
    oss << k << '=' << v << '\n';
}
void AppendKV(std::ostringstream& oss, const std::string& k, bool v) {
    oss << k << '=' << (v ? '1' : '0') << '\n';
}

}  // namespace

// ─── CameraTaskRequest serialization ─────────────────────────────────────────

std::string SerializeCameraTaskRequest(const CameraTaskRequest& req) {
    std::ostringstream oss;
    AppendKV(oss, "type",               "task_request");
    AppendKV(oss, "task_type",          static_cast<int>(req.type));
    AppendKV(oss, "task_id",            req.task_id);
    AppendKV(oss, "repeat_count",       req.repeat_count);
    AppendKV(oss, "output_dir",         req.output_dir);
    AppendKV(oss, "remote_path",        req.remote_path);
    AppendKV(oss, "local_path",         req.local_path);
    AppendKV(oss, "extended_mode",      req.extended_mode);
    AppendKV(oss, "batch_time_from",    req.batch_time_from);
    AppendKV(oss, "batch_time_to",      req.batch_time_to);
    AppendKV(oss, "target_camera_serial", req.target_camera_serial);
    AppendKV(oss, "delete_confirmed",   req.delete_confirmed);
    for (const auto& [k, v] : req.parameters) {
        AppendKV(oss, "p:" + k, v);
    }
    return oss.str();
}

bool ParseCameraTaskRequest(const std::string& msg, CameraTaskRequest* out) {
    if (out == nullptr) return false;
    const auto kv = ParseKV(msg);

    const auto type_it = kv.find("type");
    if (type_it == kv.end() || type_it->second != "task_request") return false;

    const auto get = [&](const std::string& key, const std::string& def = {}) -> std::string {
        const auto it = kv.find(key);
        return (it != kv.end()) ? it->second : def;
    };

    out->type               = static_cast<CameraTaskType>(std::stoi(get("task_type", "0")));
    out->task_id            = std::stoi(get("task_id", "0"));
    out->repeat_count       = std::stoi(get("repeat_count", "1"));
    out->output_dir         = get("output_dir");
    out->remote_path        = get("remote_path");
    out->local_path         = get("local_path");
    out->extended_mode      = get("extended_mode");
    out->batch_time_from    = get("batch_time_from");
    out->batch_time_to      = get("batch_time_to");
    out->target_camera_serial = get("target_camera_serial");
    out->delete_confirmed   = (get("delete_confirmed", "0") == "1");

    out->parameters.clear();
    for (const auto& [k, v] : kv) {
        if (k.size() > 2 && k.substr(0, 2) == "p:") {
            out->parameters[k.substr(2)] = v;
        }
    }
    return true;
}

// ─── CameraControllerSnapshot serialization ──────────────────────────────────

std::string SerializeCameraSnapshot(const CameraControllerSnapshot& snap) {
    std::ostringstream oss;
    AppendKV(oss, "type",                "snapshot");
    AppendKV(oss, "device_state",        static_cast<int>(snap.device_state));
    AppendKV(oss, "task_type",           static_cast<int>(snap.task_type));
    AppendKV(oss, "task_state",          static_cast<int>(snap.task_state));
    AppendKV(oss, "recover_attempts",    snap.recover_attempts);
    AppendKV(oss, "completed_iterations",snap.completed_iterations);
    AppendKV(oss, "total_iterations",    snap.total_iterations);
    AppendKV(oss, "result_summary",      snap.result_summary);
    AppendKV(oss, "error_message",       snap.error_message);
    // CameraRuntimeStatus sub-fields
    const auto& rs = snap.runtime_status;
    AppendKV(oss, "rs_connected",        rs.connected);
    AppendKV(oss, "rs_capturing",        rs.capturing);
    AppendKV(oss, "rs_battery_ok",       rs.battery_ok);
    AppendKV(oss, "rs_storage_ok",       rs.storage_ok);
    AppendKV(oss, "rs_media_time_ok",    rs.media_time_ok);
    AppendKV(oss, "rs_battery_level",    static_cast<int>(rs.battery.battery_level));
    AppendKV(oss, "rs_battery_scale",    static_cast<int>(rs.battery.battery_scale));
    AppendKV(oss, "rs_battery_type",     rs.battery.power_type);
    AppendKV(oss, "rs_storage_state",    rs.storage.state);
    AppendKV(oss, "rs_storage_free",     rs.storage.free_space);
    AppendKV(oss, "rs_storage_total",    rs.storage.total_space);
    AppendKV(oss, "rs_media_time",       rs.media_time);
    AppendKV(oss, "rs_recent_error",     rs.recent_error);
    return oss.str();
}

bool ParseCameraSnapshot(const std::string& msg, CameraControllerSnapshot* out) {
    if (out == nullptr) return false;
    const auto kv = ParseKV(msg);

    const auto type_it = kv.find("type");
    if (type_it == kv.end() || type_it->second != "snapshot") return false;

    const auto geti = [&](const std::string& key, int def = 0) -> int {
        const auto it = kv.find(key);
        if (it == kv.end() || it->second.empty()) return def;
        try { return std::stoi(it->second); } catch (...) { return def; }
    };
    const auto gets = [&](const std::string& key) -> std::string {
        const auto it = kv.find(key);
        return (it != kv.end()) ? it->second : std::string{};
    };
    const auto getb = [&](const std::string& key) -> bool {
        return geti(key) != 0;
    };

    out->device_state         = static_cast<CameraDeviceState>(geti("device_state"));
    out->task_type            = static_cast<CameraTaskType>(geti("task_type"));
    out->task_state           = static_cast<CameraTaskState>(geti("task_state"));
    out->recover_attempts     = geti("recover_attempts");
    out->completed_iterations = geti("completed_iterations");
    out->total_iterations     = geti("total_iterations");
    out->result_summary       = gets("result_summary");
    out->error_message        = gets("error_message");

    auto& rs = out->runtime_status;
    rs.connected      = getb("rs_connected");
    rs.capturing      = getb("rs_capturing");
    rs.battery_ok     = getb("rs_battery_ok");
    rs.storage_ok     = getb("rs_storage_ok");
    rs.media_time_ok  = getb("rs_media_time_ok");
    rs.battery.battery_level = static_cast<uint32_t>(geti("rs_battery_level"));
    rs.battery.battery_scale = static_cast<uint32_t>(geti("rs_battery_scale"));
    rs.battery.power_type    = gets("rs_battery_type");
    rs.storage.state         = gets("rs_storage_state");
    rs.media_time            = static_cast<int64_t>(std::stoll(
        kv.count("rs_media_time") ? kv.at("rs_media_time") : "0"));
    rs.recent_error          = gets("rs_recent_error");

    const auto free_it  = kv.find("rs_storage_free");
    const auto total_it = kv.find("rs_storage_total");
    rs.storage.free_space  = (free_it  != kv.end()) ? std::stoull(free_it->second)  : 0;
    rs.storage.total_space = (total_it != kv.end()) ? std::stoull(total_it->second) : 0;

    return true;
}

// ─── Child process main loop ──────────────────────────────────────────────────

void CameraWorkerChildMain(int child_fd, const CameraAppOptions& options) {
    // Ignore SIGPIPE so broken channel doesn't kill us unexpectedly.
    ::signal(SIGPIPE, SIG_IGN);

    std::cout << "[camera_worker] starting pid=" << ::getpid() << "\n";

    WorkerChannel channel(child_fd);

    // The camera manager (and therefore CameraSDK) lives entirely in this child.
    CameraManager camera_manager;
    camera_manager.Configure(options);

    using Clock    = std::chrono::steady_clock;
    using Ms       = std::chrono::milliseconds;
    const auto kSnapshotInterval = Ms(500);
    const auto kTickInterval     = Ms(100);

    auto last_snapshot = Clock::now();

    while (true) {
        // ── Receive task requests from parent (non-blocking) ─────────────────
        std::string msg;
        bool chan_err = false;
        while (channel.TryRecv(&msg, &chan_err)) {
            CameraTaskRequest req;
            if (ParseCameraTaskRequest(msg, &req)) {
                std::cout << "[camera_worker] received task type="
                          << static_cast<int>(req.type) << "\n";
                camera_manager.StartTask(req);
            }
        }
        if (chan_err) {
            std::cerr << "[camera_worker] channel broken, exiting\n";
            break;
        }

        // ── Drive the camera state machine ───────────────────────────────────
        camera_manager.Tick();

        // ── Periodically push snapshot to parent ─────────────────────────────
        const auto now = Clock::now();
        if (now - last_snapshot >= kSnapshotInterval) {
            const CameraControllerSnapshot& snap = camera_manager.snapshot();
            std::cout << "[camera_worker] snapshot send device_state="
                      << static_cast<int>(snap.device_state)
                      << " task_type=" << static_cast<int>(snap.task_type)
                      << " task_state=" << static_cast<int>(snap.task_state)
                      << " connected=" << (snap.runtime_status.connected ? "true" : "false")
                      << " media_time_ok=" << (snap.runtime_status.media_time_ok ? "true" : "false")
                      << " result_summary=" << snap.result_summary
                      << " recent_error=" << snap.runtime_status.recent_error
                      << std::endl;
            const std::string snap_msg = SerializeCameraSnapshot(snap);
            if (!channel.Send(snap_msg)) {
                std::cerr << "[camera_worker] failed to send snapshot, exiting\n";
                break;
            }
            last_snapshot = now;
        }

        std::this_thread::sleep_for(kTickInterval);
    }

    std::cout << "[camera_worker] exiting pid=" << ::getpid() << "\n";
}

// ─── Parent-side proxy ────────────────────────────────────────────────────────

CameraWorkerProxy::CameraWorkerProxy(WorkerSupervisor* supervisor, std::string worker_name)
    : supervisor_(supervisor), worker_name_(std::move(worker_name)) {}

int CameraWorkerProxy::restart_count() const {
    return supervisor_ ? supervisor_->RestartCount(worker_name_) : 0;
}

void CameraWorkerProxy::Tick() {
    if (supervisor_ == nullptr) return;

    const int fd = supervisor_->GetParentFd(worker_name_);
    if (fd < 0) {
        channel_alive_ = false;
        return;
    }

    // Detect worker restart: fd changed → reset state
    if (fd != last_seen_fd_) {
        last_seen_fd_  = fd;
        channel_alive_ = true;
        task_active_   = false;
        task_finished_ = false;
        cached_snapshot_ = {};
        std::cout << "[CameraWorkerProxy] worker (re)connected fd=" << fd << "\n";
    }

    // Drain all pending snapshot messages (non-blocking).
    // CAM-005: on board, child snapshots are observable on the sender side but a poll()+recv()
    // gate in the parent can miss ready datagrams. Read directly with MSG_DONTWAIT until empty.
    char buf[kWorkerMaxMessageSize];
    while (true) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n > 0) {
            ProcessMessage(std::string(buf, static_cast<size_t>(n)));
            continue;
        }
        if (n == 0) {
            channel_alive_ = false;
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        channel_alive_ = false;
        break;
    }
}

bool CameraWorkerProxy::StartTask(const CameraTaskRequest& request) {
    if (supervisor_ == nullptr) return false;
    const int fd = supervisor_->GetParentFd(worker_name_);
    if (fd < 0) {
        std::cerr << "[CameraWorkerProxy] StartTask: worker not running\n";
        return false;
    }
    const std::string msg = SerializeCameraTaskRequest(request);
    if (msg.size() > kWorkerMaxMessageSize) {
        std::cerr << "[CameraWorkerProxy] task request too large\n";
        return false;
    }
    const ssize_t n = ::send(fd, msg.data(), msg.size(), MSG_NOSIGNAL);
    if (n != static_cast<ssize_t>(msg.size())) {
        return false;
    }

    // The cached snapshot still describes the previous task until the child
    // publishes its next snapshot. Mark the new task queued immediately so
    // TaskManager cannot count the previous completion a second time.
    task_active_ = true;
    task_finished_ = false;
    cached_snapshot_.task_type = request.type;
    cached_snapshot_.task_state = CameraTaskState::Queued;
    cached_snapshot_.result_summary.clear();
    cached_snapshot_.error_message.clear();
    return true;
}

bool CameraWorkerProxy::HasActiveTask() const  { return task_active_;   }
bool CameraWorkerProxy::IsTaskFinished() const { return task_finished_;  }

const CameraControllerSnapshot& CameraWorkerProxy::snapshot() const {
    return cached_snapshot_;
}

void CameraWorkerProxy::ProcessMessage(const std::string& msg) {
    CameraControllerSnapshot snap;
    if (!ParseCameraSnapshot(msg, &snap)) {
        std::cerr << "[CameraWorkerProxy] ignored malformed message bytes="
                  << msg.size() << std::endl;
        return;
    }

    cached_snapshot_ = snap;
    std::cout << "[CameraWorkerProxy] snapshot recv device_state="
              << static_cast<int>(snap.device_state)
              << " task_type=" << static_cast<int>(snap.task_type)
              << " task_state=" << static_cast<int>(snap.task_state)
              << " connected=" << (snap.runtime_status.connected ? "true" : "false")
              << " media_time_ok=" << (snap.runtime_status.media_time_ok ? "true" : "false")
              << " result_summary=" << snap.result_summary
              << " recent_error=" << snap.runtime_status.recent_error
              << std::endl;

    // Derive active / finished flags from task state
    const auto ts = snap.task_state;
    task_active_ = (ts != CameraTaskState::None      &&
                    ts != CameraTaskState::Completed  &&
                    ts != CameraTaskState::Failed     &&
                    ts != CameraTaskState::Timeout    &&
                    ts != CameraTaskState::Cancelled);
    task_finished_ = (ts == CameraTaskState::Completed ||
                      ts == CameraTaskState::Failed    ||
                      ts == CameraTaskState::Timeout);
}

}  // namespace youyeetoo
