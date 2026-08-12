#include "watchdog_service.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
// Linux watchdog ioctl 头文件
#include <linux/watchdog.h>

#include <cerrno>
#include <cstring>
#include <iostream>

namespace youyeetoo {

// ─── 析构 ─────────────────────────────────────────────────────────────────────

WatchdogService::~WatchdogService() {
    Close();
}

// ─── 打开 ─────────────────────────────────────────────────────────────────────

bool WatchdogService::Open(const WatchdogConfig& config) {
    config_ = config;
    last_feed_ = std::chrono::steady_clock::now();

    // 1. 硬件看门狗
    if (!config.hw_watchdog_path.empty()) {
        OpenHwWatchdog(config.hw_watchdog_path, config.hw_timeout_sec);
    }

    // 2. 检测 systemd NOTIFY_SOCKET
    if (config.enable_sd_notify) {
        sd_notify_available_ = (::getenv("NOTIFY_SOCKET") != nullptr);
        if (sd_notify_available_) {
            SdNotify("READY=1");
            std::cout << "[WatchdogService] systemd notify socket available\n";
        } else {
            std::cout << "[WatchdogService] NOTIFY_SOCKET not set"
                         " (service not managed by systemd or WatchdogSec not configured)\n";
        }
    }

    std::cout << "[WatchdogService] open complete"
              << " hw=" << (hw_fd_ >= 0 ? config.hw_watchdog_path : "disabled")
              << " feed_interval=" << config.feed_interval.count() << "s\n";
    return true;
}

bool WatchdogService::OpenHwWatchdog(const std::string& path, int timeout_sec) {
    hw_fd_ = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (hw_fd_ < 0) {
        std::cerr << "[WatchdogService] open " << path
                  << " failed: " << std::strerror(errno)
                  << " (hardware watchdog disabled)\n";
        return false;
    }

    // 设置超时时间
    int actual_timeout = timeout_sec;
    if (::ioctl(hw_fd_, WDIOC_SETTIMEOUT, &actual_timeout) < 0) {
        std::cerr << "[WatchdogService] WDIOC_SETTIMEOUT failed: "
                  << std::strerror(errno) << "\n";
    } else {
        std::cout << "[WatchdogService] hw watchdog timeout set to "
                  << actual_timeout << "s\n";
    }

    // 首次喂狗（打开后立即喂一次）
    FeedHwWatchdog();

    std::cout << "[WatchdogService] hw watchdog opened: " << path << "\n";
    return true;
}

// ─── 关闭 ─────────────────────────────────────────────────────────────────────

void WatchdogService::Close() {
    if (sd_notify_available_) {
        SdNotify("STOPPING=1");
    }
    if (hw_fd_ >= 0) {
        // 写入 'V' 是 Linux watchdog 驱动的"安全关闭"约定。
        // 收到 'V' 后驱动不会在 close() 时触发复位。
        // 注意：不是所有驱动都支持（CONFIG_WATCHDOG_NOWAYOUT=y 时忽略）。
        const ssize_t n = ::write(hw_fd_, "V", 1);
        (void)n;
        ::close(hw_fd_);
        hw_fd_ = -1;
        std::cout << "[WatchdogService] hw watchdog closed (safe stop)\n";
    }
}

// ─── 喂狗 ─────────────────────────────────────────────────────────────────────

bool WatchdogService::Feed(bool is_healthy) {
    if (!is_healthy) {
        // 不健康时拒绝喂狗，让看门狗超时触发复位
        ++skip_count_;
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - last_feed_ < config_.feed_interval) {
        return false;  // 未到喂狗间隔
    }

    bool fed = false;

    // 喂硬件看门狗
    if (hw_fd_ >= 0) {
        fed = FeedHwWatchdog();
    }

    // 喂 systemd 看门狗
    if (sd_notify_available_) {
        SdNotify("WATCHDOG=1");
        fed = true;
    }

    if (fed) {
        ++feed_count_;
        last_feed_ = now;
    }
    return fed;
}

bool WatchdogService::FeedHwWatchdog() {
    // WDIOC_KEEPALIVE ioctl 是推荐方式
    if (::ioctl(hw_fd_, WDIOC_KEEPALIVE, 0) < 0) {
        // fallback: 写任意一个字节
        const ssize_t n = ::write(hw_fd_, "1", 1);
        if (n < 0) {
            std::cerr << "[WatchdogService] feed failed: " << std::strerror(errno) << "\n";
            return false;
        }
    }
    return true;
}

// ─── systemd 通知（不依赖 libsystemd）────────────────────────────────────────

bool WatchdogService::SdNotify(const std::string& message) const {
    const char* socket_path = ::getenv("NOTIFY_SOCKET");
    if (socket_path == nullptr) return false;

    const int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;

    if (socket_path[0] == '@') {
        // 抽象命名空间套接字（以 @ 开头）
        addr.sun_path[0] = '\0';
        ::strncpy(addr.sun_path + 1, socket_path + 1, sizeof(addr.sun_path) - 2);
    } else {
        ::strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    }

    const socklen_t addr_len = offsetof(struct sockaddr_un, sun_path) +
                               ::strlen(addr.sun_path + (socket_path[0] == '@' ? 0 : 0)) + 1;

    const ssize_t n = ::sendto(fd, message.data(), message.size(), MSG_NOSIGNAL,
                               reinterpret_cast<struct sockaddr*>(&addr), addr_len);
    ::close(fd);
    return n == static_cast<ssize_t>(message.size());
}

}  // namespace youyeetoo
