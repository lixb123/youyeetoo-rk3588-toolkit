#include "can_worker.h"

// SocketCAN 内核头文件（Linux 专有）
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "can_protocol.h"
#include "worker_channel.h"
#include "worker_supervisor.h"

namespace youyeetoo {

// ─── 子进程内部工具 ───────────────────────────────────────────────────────────

namespace {

// 打开一个 SocketCAN 接口，返回 fd（≥0）或 -1（失败时打印原因）。
// 不负责设置波特率；由系统层（ip link / networkd）提前配置好。
int OpenCanSocket(const std::string& ifname) {
    const int s = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        std::cerr << "[can_worker] socket() failed for " << ifname
                  << ": " << std::strerror(errno) << "\n";
        return -1;
    }

    struct ifreq ifr {};
    std::strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    if (::ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        std::cerr << "[can_worker] SIOCGIFINDEX failed for " << ifname
                  << ": " << std::strerror(errno) << "\n";
        ::close(s);
        return -1;
    }

    struct sockaddr_can addr {};
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (::bind(s, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[can_worker] bind() failed for " << ifname
                  << ": " << std::strerror(errno) << "\n";
        ::close(s);
        return -1;
    }

    std::cout << "[can_worker] opened " << ifname << " fd=" << s << "\n";
    return s;
}

// 将一行指令写入 telemetry_command_inbox（append 模式）。
// TelemetryCommandInterface 每轮读取并处理 inbox 的第一行，同时回写剩余行。
// 多进程并发写：由于单行很短（< 100 字节），Linux ext4 的 O_APPEND 写入在
// 实际使用中几乎不会与父进程的 truncate 产生冲突；星载系统指令频率低，
// 偶发丢失时由地面重发。
bool AppendCommandToInbox(const std::string& inbox_path,
                          const std::string& command_name,
                          const std::string& extra_params) {
    std::string line = command_name;
    if (!extra_params.empty()) {
        line += ' ';
        line += extra_params;
    }
    line += '\n';

    // O_APPEND 保证每次 write 原子追加到文件末尾（POSIX）
    std::ofstream f(inbox_path, std::ios::app);
    if (!f.is_open()) {
        std::cerr << "[can_worker] cannot append to inbox " << inbox_path << "\n";
        return false;
    }
    f << line;
    return f.good();
}

// 从 "type=can_tx\ncan_id=...\ndata=...\n" IPC 消息中解析 CAN ID 和 data。
// 这是父进程通过 CanWorkerProxy::SendFrame() 发来的 TX 请求。
bool ParseTxMessage(const std::string& msg, uint32_t* can_id_out,
                    uint8_t* data_out, uint8_t* dlc_out) {
    uint32_t cid = 0;
    uint8_t  buf[8]{};
    uint8_t  dlc = 0;
    bool     has_id   = false;
    bool     has_data = false;

    std::istringstream ss(msg);
    std::string line;
    while (std::getline(ss, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);

        if (key == "can_id") {
            try { cid = static_cast<uint32_t>(std::stoul(val, nullptr, 0)); has_id = true; }
            catch (...) {}
        } else if (key == "data") {
            // 期望十六进制字节串，例如 "0102030405060708"
            const size_t byte_count = std::min(val.size() / 2, size_t{8});
            for (size_t i = 0; i < byte_count; ++i) {
                try {
                    buf[i] = static_cast<uint8_t>(std::stoul(val.substr(i * 2, 2), nullptr, 16));
                } catch (...) { buf[i] = 0; }
            }
            dlc = static_cast<uint8_t>(byte_count);
            has_data = true;
        }
    }

    if (!has_id || !has_data) return false;
    *can_id_out = cid;
    std::memcpy(data_out, buf, dlc);
    *dlc_out = dlc;
    return true;
}

}  // namespace

// ─── CanWorkerChildMain ───────────────────────────────────────────────────────
//
// 在独立子进程中运行（WorkerSupervisor::SpawnOne 调用）。
// 子进程崩溃时 WorkerSupervisor 检测到 SIGCHLD 并以退避重启。
//
// 设计边界：
//   ● 此函数只调用 CanProtocol（协议层），不含业务逻辑
//   ● 通过 AppendCommandToInbox() 把解析结果注入到与父进程共享的 inbox 文件
//   ● 指令注入点（inbox 文件）不变，保持与 TelemetryCommandInterface 的接口稳定

void CanWorkerChildMain(int child_fd, const CameraAppOptions& options) {
    ::signal(SIGPIPE, SIG_IGN);
    std::cout << "[can_worker] starting pid=" << ::getpid() << "\n";

    WorkerChannel channel(child_fd);

    // ── 1. 加载配置 ───────────────────────────────────────────────────────────
    CanConfig cfg;
    {
        std::string load_err;
        const bool ok = cfg.LoadFromFile(options.can_config_path, &load_err);
        if (!ok) {
            std::cerr << "[can_worker] config: " << load_err << "\n";
        } else {
            std::cout << "[can_worker] config loaded: " << options.can_config_path << "\n";
        }
        std::cout << "[can_worker] primary=" << cfg.primary_interface
                  << " backup=" << cfg.backup_interface
                  << " rx_id=0x" << std::hex << cfg.rx_command_id
                  << " tx_id=0x" << cfg.tx_telemetry_id
                  << std::dec << "\n";
    }

    // ── 2. 加载指令映射 ───────────────────────────────────────────────────────
    CanProtocol protocol;
    {
        std::string map_err;
        const bool ok = protocol.LoadCommandMap(cfg.command_map_path, &map_err);
        if (!ok) {
            std::cerr << "[can_worker] command map: " << map_err << "\n";
        } else {
            std::cout << "[can_worker] command map loaded: " << cfg.command_map_path
                      << " entries=" << protocol.CommandMapSize() << "\n";
        }
    }

    // ── 3. 打开 SocketCAN 接口 ────────────────────────────────────────────────
    int primary_fd = OpenCanSocket(cfg.primary_interface);
    int backup_fd  = OpenCanSocket(cfg.backup_interface);

    if (primary_fd < 0 && backup_fd < 0) {
        std::cerr << "[can_worker] no CAN interface available"
                  << " (primary=" << cfg.primary_interface
                  << " backup=" << cfg.backup_interface
                  << "); running in no-hardware mode\n";
    }

    // 当前活动接口
    bool using_backup   = false;
    int  active_tx_fd   = (primary_fd >= 0) ? primary_fd : backup_fd;

    // ── 4. 计数器 ─────────────────────────────────────────────────────────────
    uint64_t rx_count      = 0;
    uint64_t tx_count      = 0;
    uint64_t error_count   = 0;
    int      primary_errors = 0;

    using Clock = std::chrono::steady_clock;
    using Ms    = std::chrono::milliseconds;

    auto last_status    = Clock::now();
    auto last_telemetry = Clock::now();
    const auto kStatusInterval    = Ms(1000);
    const Ms   kTelemetryInterval = Ms(cfg.telemetry_interval_ms);

    // ── 5. 主事件循环 ─────────────────────────────────────────────────────────
    while (true) {
        // 构造 poll fd 集合
        // pfds[0] = IPC channel（来自父进程的 TX 请求）
        // pfds[1] = primary CAN fd（若可用）
        // pfds[2] = backup  CAN fd（若可用且不同于 primary）
        struct pollfd pfds[3];
        int nfds = 0;

        pfds[nfds++] = {child_fd, POLLIN, 0};
        if (primary_fd >= 0) pfds[nfds++] = {primary_fd, POLLIN, 0};
        if (backup_fd  >= 0 && backup_fd != primary_fd)
            pfds[nfds++] = {backup_fd, POLLIN, 0};

        const int ret = ::poll(pfds, nfds, 50 /* ms 超时，保证状态上报及时 */);
        if (ret < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[can_worker] poll() error: " << std::strerror(errno) << "\n";
            break;
        }

        // ── 5.1 处理来自父进程的消息（TX 请求 / 未来扩展）─────────────────────
        if (pfds[0].revents & (POLLHUP | POLLERR)) {
            std::cerr << "[can_worker] IPC channel closed, exiting\n";
            break;
        }
        if (pfds[0].revents & POLLIN) {
            std::string msg;
            bool chan_err = false;
            while (channel.TryRecv(&msg, &chan_err)) {
                if (msg.find("type=can_tx") != std::string::npos) {
                    // 父进程请求发送一帧 CAN 数据
                    uint32_t cid = 0;
                    uint8_t  tx_data[8]{};
                    uint8_t  tx_dlc = 0;
                    if (ParseTxMessage(msg, &cid, tx_data, &tx_dlc) && active_tx_fd >= 0) {
                        struct ::can_frame tx_frame {};
                        tx_frame.can_id  = cid;
                        tx_frame.can_dlc = tx_dlc;
                        std::memcpy(tx_frame.data, tx_data, tx_dlc);
                        if (::write(active_tx_fd, &tx_frame, sizeof(tx_frame)) < 0) {
                            ++error_count;
                            std::cerr << "[can_worker] TX write failed: "
                                      << std::strerror(errno) << "\n";
                        } else {
                            ++tx_count;
                        }
                    }
                }
                // 未来可在此处理其他消息类型
            }
            if (chan_err) {
                std::cerr << "[can_worker] channel broken, exiting\n";
                break;
            }
        }

        // ── 5.2 处理 CAN RX ──────────────────────────────────────────────────
        // 接收并解析来自 CAN 总线的帧；主备路都监听，均可注入指令
        for (int i = 1; i < nfds; ++i) {
            if (!(pfds[i].revents & POLLIN)) continue;

            const bool is_primary = (pfds[i].fd == primary_fd);
            const std::string iface_name =
                is_primary ? cfg.primary_interface : cfg.backup_interface;

            struct ::can_frame kframe {};
            const ssize_t n = ::read(pfds[i].fd, &kframe, sizeof(kframe));
            if (n < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    ++error_count;
                    if (is_primary) ++primary_errors;
                    std::cerr << "[can_worker] read error on " << iface_name
                              << ": " << std::strerror(errno) << "\n";
                }
                continue;
            }
            if (n < static_cast<ssize_t>(sizeof(struct ::can_frame))) continue;

            ++rx_count;
            if (is_primary) primary_errors = 0;  // 成功收帧时重置错误计数

            // 转换为应用层 CanFrame，与内核 struct can_frame 解耦
            CanFrame cf;
            cf.can_id = kframe.can_id & CAN_EFF_MASK;  // 去掉标志位
            cf.dlc    = std::min(static_cast<uint8_t>(kframe.can_dlc), uint8_t{8});
            std::memcpy(cf.data, kframe.data, cf.dlc);

            // ── 纯协议解析（无业务逻辑）────────────────────────────────────
            const ParsedCanCommand cmd = protocol.ParseRxFrame(cf, cfg.rx_command_id);

            if (!cmd.valid) {
                // 非指令帧（其他设备遥测等），仅 debug 级日志
                if (!cmd.error_description.empty()) {
                    std::cerr << "[can_worker] rx non-command"
                              << " iface=" << iface_name
                              << " can_id=0x" << std::hex << cf.can_id << std::dec
                              << " reason=" << cmd.error_description << "\n";
                }
                continue;
            }

            std::cout << "[can_worker] rx command=" << cmd.command_name
                      << (cmd.extra_params.empty() ? "" : " params=" + cmd.extra_params)
                      << " iface=" << iface_name
                      << " can_id=0x" << std::hex << cf.can_id << std::dec << "\n";

            // ── 注入到 telemetry_command_inbox（指令注入点不变）──────────────
            // TelemetryCommandInterface 在父进程中每 loop_interval_ms 读取此文件
            if (!options.telemetry_command_inbox.empty()) {
                AppendCommandToInbox(options.telemetry_command_inbox,
                                     cmd.command_name, cmd.extra_params);
            }
        }

        // ── 5.3 主/备路切换 ───────────────────────────────────────────────────
        if (!using_backup && primary_errors >= cfg.rx_error_threshold && backup_fd >= 0) {
            std::cerr << "[can_worker] primary error threshold reached"
                      << " errors=" << primary_errors
                      << ", switching to backup=" << cfg.backup_interface << "\n";
            using_backup  = true;
            active_tx_fd  = backup_fd;
        }
        // 备路中若 primary 恢复（primary_errors 已归零），主动切回（保守策略：不自动切回，
        // 等待下次子进程重启后重新评估。如需自动切回可在此补充逻辑）

        // ── 5.4 周期发送遥测帧 ────────────────────────────────────────────────
        const auto now = Clock::now();
        if (active_tx_fd >= 0 && now - last_telemetry >= kTelemetryInterval) {
            last_telemetry = now;

            const std::string compact =
                std::string(using_backup ? "bk" : "ok") +
                " r=" + std::to_string(rx_count) +
                " t=" + std::to_string(tx_count) +
                " e=" + std::to_string(error_count);

            const CanFrame tf = protocol.BuildTelemetryFrame(cfg.tx_telemetry_id, compact);

            struct ::can_frame tx_frame {};
            tx_frame.can_id  = tf.can_id;
            tx_frame.can_dlc = tf.dlc;
            std::memcpy(tx_frame.data, tf.data, tf.dlc);

            if (::write(active_tx_fd, &tx_frame, sizeof(tx_frame)) < 0) {
                ++error_count;
                std::cerr << "[can_worker] telemetry TX failed: "
                          << std::strerror(errno) << "\n";
            } else {
                ++tx_count;
            }
        }

        // ── 5.5 周期上报状态到父进程 ─────────────────────────────────────────
        if (now - last_status >= kStatusInterval) {
            last_status = now;

            const std::string iface  = using_backup ? cfg.backup_interface
                                                     : cfg.primary_interface;
            const std::string hw     = (active_tx_fd >= 0) ? "ok" : "no_hw";
            const std::string role   = using_backup ? "backup" : "primary";

            std::ostringstream oss;
            oss << "type=can_status\n"
                << "status=can=" << hw
                << " iface=" << iface
                << " role=" << role
                << " rx=" << rx_count
                << " tx=" << tx_count
                << " err=" << error_count
                << '\n';

            if (!channel.Send(oss.str())) {
                std::cerr << "[can_worker] send status failed, exiting\n";
                break;
            }
        }
    }

    // ── 清理 ──────────────────────────────────────────────────────────────────
    if (primary_fd >= 0) ::close(primary_fd);
    if (backup_fd  >= 0 && backup_fd != primary_fd) ::close(backup_fd);
    std::cout << "[can_worker] exiting pid=" << ::getpid() << "\n";
}

// ─── Parent-side proxy ────────────────────────────────────────────────────────

CanWorkerProxy::CanWorkerProxy(WorkerSupervisor* supervisor, std::string worker_name)
    : supervisor_(supervisor), worker_name_(std::move(worker_name)) {}

void CanWorkerProxy::Tick() {
    if (supervisor_ == nullptr) return;

    const int fd = supervisor_->GetParentFd(worker_name_);
    if (fd < 0) {
        channel_alive_ = false;
        return;
    }

    if (fd != last_seen_fd_) {
        // worker 重启后 fd 会变化
        last_seen_fd_  = fd;
        channel_alive_ = true;
        status_summary_ = "can=subprocess_starting";
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

        // 从 "type=can_status\nstatus=..." 中提取 status= 字段用于日志/遥测
        const std::string msg(buf, static_cast<size_t>(n));
        const auto pos = msg.find("status=");
        if (pos != std::string::npos) {
            const auto end = msg.find('\n', pos);
            status_summary_ = msg.substr(
                pos + 7,
                end == std::string::npos ? std::string::npos : end - pos - 7);
        }
    }
}

bool CanWorkerProxy::SendFrame(const std::string& can_id_hex,
                               const std::string& data_hex) {
    if (supervisor_ == nullptr) return false;
    const int fd = supervisor_->GetParentFd(worker_name_);
    if (fd < 0) return false;

    // 子进程 ParseTxMessage() 期望此格式
    const std::string msg = "type=can_tx\ncan_id=" + can_id_hex +
                            "\ndata=" + data_hex + "\n";
    return ::send(fd, msg.data(), msg.size(), MSG_NOSIGNAL) ==
           static_cast<ssize_t>(msg.size());
}

}  // namespace youyeetoo
