#pragma once

#include <chrono>
#include <string>

namespace youyeetoo {

// ─── WatchdogService ──────────────────────────────────────────────────────────
//
// 实现 SYS-004：定期向硬件看门狗和 systemd 看门狗报告健康状态。
//
// 两层对接：
//   1. 硬件看门狗  /dev/watchdog  ── ioctl WDIOC_KEEPALIVE
//      超时未喂 → 板级硬件复位（最终安全保底）
//
//   2. systemd 看门狗  NOTIFY_SOCKET ── "WATCHDOG=1"
//      超时未喂 → systemd 重启 service（软件层保底）
//      （不依赖 libsystemd，直接写 NOTIFY_SOCKET 的 UNIX 套接字）
//
// 喂狗策略（SYS-004）：
//   只有 is_healthy=true 时才喂狗。
//   is_healthy 由调用方根据以下条件评估：
//     - 主循环正常迭代（能走到 Feed() 说明主线程没死）
//     - CAN worker 和 telemetry worker 在线
//   相机是可选外设；未连接、初始化失败或暂时不可用属于降级状态，
//   不应触发整板硬件复位。相机 worker 的进程存活由 WorkerSupervisor 管理。
//   核心条件不满足时，拒绝喂狗，等待看门狗超时触发复位。
//
// ─────────────────────────────────────────────────────────────────────────────

struct WatchdogConfig {
    // 硬件看门狗设备路径（空字符串 = 不启用硬件看门狗）
    std::string hw_watchdog_path = "/dev/watchdog";

    // 硬件看门狗超时时间（秒）。
    // 实际复位时间 = hw_timeout_sec，喂狗间隔建议 ≤ hw_timeout_sec / 2。
    int hw_timeout_sec = 30;

    // 喂狗间隔。必须 < min(hw_timeout_sec, WatchdogSec in service file) / 2。
    std::chrono::seconds feed_interval{10};

    // 如果 systemd 没有设置 WatchdogSec，sd_notify 喂狗无效果但不报错。
    bool enable_sd_notify = true;
};

class WatchdogService {
public:
    WatchdogService() = default;
    ~WatchdogService();

    WatchdogService(const WatchdogService&) = delete;
    WatchdogService& operator=(const WatchdogService&) = delete;

    // 打开硬件看门狗设备，设置超时，发送 systemd READY=1。
    // 失败时打印警告但不 abort（板端可能没有 /dev/watchdog 驱动）。
    bool Open(const WatchdogConfig& config);

    // 关闭硬件看门狗（写入 'V' 以安全关闭，避免复位）。
    void Close();

    // 主循环调用：如果 is_healthy=true 且距上次喂狗已超过 feed_interval，则喂狗。
    // 如果 is_healthy=false，什么都不做（让看门狗超时触发复位）。
    // 返回 true 表示本次执行了喂狗动作。
    bool Feed(bool is_healthy);

    // 查询状态
    bool hw_watchdog_open()    const { return hw_fd_ >= 0; }
    bool sd_notify_available() const { return sd_notify_available_; }
    int  feed_count()          const { return feed_count_; }
    int  skip_count()          const { return skip_count_; }

    // 发送任意 systemd 通知（READY=1, STOPPING=1 等）
    bool SdNotify(const std::string& message) const;

private:
    bool OpenHwWatchdog(const std::string& path, int timeout_sec);
    bool FeedHwWatchdog();

    WatchdogConfig config_;
    int  hw_fd_              = -1;
    bool sd_notify_available_ = false;
    int  feed_count_          = 0;
    int  skip_count_          = 0;
    std::chrono::steady_clock::time_point last_feed_{};
};

}  // namespace youyeetoo
