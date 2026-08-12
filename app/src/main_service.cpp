#include "main_service.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "camera_worker.h"
#include "can_worker.h"
#include "command_dispatcher.h"
#include "log_service.h"
#include "service_context.h"
#include "task_manager.h"
#include "telemetry_command_interface.h"
#include "telemetry_manager.h"
#include "telemetry_worker.h"
#include "usb_service.h"
#include "watchdog_service.h"
#include "worker_supervisor.h"

namespace youyeetoo {
namespace {

bool g_debug_logging_enabled = false;

enum class LogLevel { Error, Warn, Info, Debug };

std::string LogLevelToString(LogLevel level) {
    switch (level) {
    case LogLevel::Error: return "ERROR";
    case LogLevel::Warn:  return "WARN";
    case LogLevel::Info:  return "INFO";
    case LogLevel::Debug: return "DEBUG";
    }
    return "INFO";
}

std::string NowTimestamp() {
    const auto now      = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#if defined(_WIN32)
    localtime_s(&local_tm, &t);
#else
    local_tm = *std::localtime(&t);
#endif
    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%F %T");
    return oss.str();
}

void Log(LogLevel level, const std::string& message) {
    if (level == LogLevel::Debug && !g_debug_logging_enabled) return;
    std::cout << "[" << NowTimestamp() << "] "
              << "[" << LogLevelToString(level) << "] "
              << message << std::endl;
}

}  // namespace

// ─── MainServiceApp::Run ──────────────────────────────────────────────────────
//
// Process tree after startup:
//
//   youyeetoo_app (main)
//     ├── camera_worker      ← owns CameraSDK; if it crashes, SDK is isolated
//     ├── can_worker         ← owns CAN socket(s)
//     └── telemetry_worker   ← owns /proc reads, thermal sensor reads
//
// Each worker communicates with the parent via a SOCK_SEQPACKET channel pair.
// WorkerSupervisor monitors child PIDs via waitpid(WNOHANG) on each tick and
// restarts dead workers with exponential back-off (1 s → … → 30 s).
// ─────────────────────────────────────────────────────────────────────────────

int MainServiceApp::Run(const CameraAppOptions& options) {
    g_debug_logging_enabled = options.debug;

    // ── 1. Create and start worker supervisor ─────────────────────────────────
    WorkerSupervisor supervisor;

    supervisor.Register({"camera_worker", CameraWorkerChildMain});
    supervisor.Register({"can_worker",    CanWorkerChildMain});
    supervisor.Register({"telemetry_worker", TelemetryWorkerChildMain});

    supervisor.StartAll(options);

    // ── 2. 看门狗（尽早打开，避免启动耗时过长触发超时）────────────────────────
    WatchdogService watchdog;
    {
        WatchdogConfig wdcfg;
        wdcfg.hw_watchdog_path = "/dev/watchdog";  // 板端路径；没有驱动时静默跳过
        wdcfg.hw_timeout_sec   = 30;               // 硬件超时 30s
        wdcfg.feed_interval    = std::chrono::seconds(10);  // 每 10s 喂一次
        wdcfg.enable_sd_notify = true;             // systemd WatchdogSec 联动
        watchdog.Open(wdcfg);
        // Open() 内部已发送 READY=1；不在此处重复发送
    }

    // ── 3. Parent-side proxies ─────────────────────────────────────────────────
    // CameraWorkerProxy implements ICameraManager → drop-in for CameraManager.
    CameraWorkerProxy    camera_proxy(&supervisor, "camera_worker");
    CanWorkerProxy       can_proxy(&supervisor,    "can_worker");
    TelemetryWorkerProxy telemetry_proxy(&supervisor, "telemetry_worker");

    // ── 3. In-process services (unchanged; no SDK risk) ───────────────────────
    BootStateService boot_state_service;
    boot_state_service.Configure(options);

    UsbService usb_service;
    usb_service.Configure(options);

    StorageService storage_service;
    storage_service.Configure(options);

    OtaReceiveService ota_receive_service;
    ota_receive_service.Configure(options);

    NarrowbandService narrowband_service;
    narrowband_service.Configure(options);

    DownlinkService downlink_service;
    downlink_service.Configure(options);

    LogService log_service;
    log_service.Configure(options);

    // TaskManager now takes ICameraManager* → accepts CameraWorkerProxy
    TaskManager task_manager(&camera_proxy);
    task_manager.Configure(options);

    ServiceContext service_context;
    service_context.task_manager        = &task_manager;
    service_context.boot_state_service  = &boot_state_service;
    service_context.can_service         = nullptr;  // replaced by can_proxy
    service_context.usb_service         = &usb_service;
    service_context.storage_service     = &storage_service;
    service_context.ota_receive_service = &ota_receive_service;
    service_context.narrowband_service  = &narrowband_service;
    service_context.downlink_service    = &downlink_service;
    service_context.log_service         = &log_service;

    CommandDispatcher command_dispatcher;

    TelemetryCommandInterface command_interface;
    command_interface.Configure(options);

    std::string bootstrap_error;
    if (!task_manager.Bootstrap(&bootstrap_error)) {
        Log(LogLevel::Error, "service bootstrap failed error=" + bootstrap_error);
        return 4;
    }

    TelemetryManager telemetry_manager;
    std::string last_summary;

    Log(LogLevel::Info,
        "service loop start"
        " serial="         + options.serial_number +
        " poll_ms="        + std::to_string(options.poll_interval_ms) +
        " loop_ms="        + std::to_string(options.loop_interval_ms) +
        " timeout_ms="     + std::to_string(options.timeout_ms) +
        " workers=camera_worker,can_worker,telemetry_worker");

    int loop_count = 0;
    while (options.max_loops == 0 || loop_count < options.max_loops) {
        ++loop_count;

        // ── Supervisor: reap dead workers, restart with back-off ──────────────
        supervisor.Tick(options);

        // ── Drive worker proxies (drain IPC messages) ─────────────────────────
        camera_proxy.Tick();
        can_proxy.Tick();
        telemetry_proxy.Tick();

        // ── Drive in-process services ─────────────────────────────────────────
        boot_state_service.Tick();
        usb_service.Tick();
        storage_service.Tick();
        ota_receive_service.Tick();
        narrowband_service.Tick();
        downlink_service.Tick();
        log_service.Tick();
        task_manager.Tick();

        // Command interface reads telemetry_command_inbox and dispatches
        command_interface.Tick(&command_dispatcher, &service_context);

        // ── 健康评估与看门狗喂食（SYS-004）─────────────────────────────────────
        // 看门狗只保护核心进程健康：
        //   1. 主循环正常迭代（能走到这里说明主线程没死）
        //   2. CAN worker 在线（可接收 / 发送星载指令）
        //   3. telemetry worker 在线（系统指标采集正常）
        //
        // 相机是可选外设。相机未连接或初始化失败时，业务状态会变为
        // Degraded/Fatal，但不应停止喂硬件狗，否则“无相机启动”会把整板
        // 复位成约 44 秒的无限循环。相机 worker 自身仍由 supervisor 拉起/重启。
        {
            const auto& cam_snap = camera_proxy.snapshot();
            const bool camera_ready = (cam_snap.device_state == CameraDeviceState::Idle ||
                                       cam_snap.device_state == CameraDeviceState::Busy);
            const bool can_ok  = can_proxy.channel_alive();
            const bool tele_ok = telemetry_proxy.channel_alive();
            const bool is_healthy = can_ok && tele_ok;

            const bool fed = watchdog.Feed(is_healthy);
            if (!is_healthy && (loop_count % 50 == 0)) {
                Log(LogLevel::Warn,
                    "watchdog_skip"
                    " can_ok="  + std::string(can_ok  ? "yes" : "no") +
                    " tele_ok=" + std::string(tele_ok ? "yes" : "no") +
                    " camera_ready=" + std::string(camera_ready ? "yes" : "no") +
                    " skip_count=" + std::to_string(watchdog.skip_count()));
            }
            if (fed && options.debug) {
                Log(LogLevel::Debug,
                    "watchdog_feed feed_count=" + std::to_string(watchdog.feed_count()));
            }
        }

        // ── Build and log snapshot ────────────────────────────────────────────
        // Use a synthetic CanService summary from can_proxy.
        CanService can_service_stub;  // used only for snapshot interface; status overridden below
        const MainServiceSnapshot snapshot =
            telemetry_manager.BuildSnapshot(loop_count,
                                            camera_proxy.snapshot(),
                                            task_manager,
                                            boot_state_service,
                                            can_service_stub,
                                            usb_service,
                                            storage_service,
                                            ota_receive_service,
                                            narrowband_service,
                                            downlink_service,
                                            log_service,
                                            command_interface);

        // Enrich summary with subprocess status
        std::string summary = TelemetryManager::BuildSummary(snapshot);
        summary += " " + can_proxy.StatusSummary();
        summary += " " + telemetry_proxy.StatusSummary();

        const int cam_restarts = supervisor.RestartCount("camera_worker");
        if (cam_restarts > 0) {
            summary += " camera_worker_restarts=" + std::to_string(cam_restarts);
        }

        if (summary != last_summary) {
            const LogLevel level =
                snapshot.lifecycle_state == ServiceLifecycleState::Fatal   ? LogLevel::Error :
                snapshot.lifecycle_state == ServiceLifecycleState::Degraded ? LogLevel::Warn
                                                                             : LogLevel::Info;
            Log(level, summary);
            last_summary = summary;
        } else {
            Log(LogLevel::Debug, "service state unchanged");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(options.loop_interval_ms));
    }

    const MainServiceSnapshot final_snapshot =
        telemetry_manager.BuildSnapshot(loop_count,
                                        camera_proxy.snapshot(),
                                        task_manager,
                                        boot_state_service,
                                        CanService{},
                                        usb_service,
                                        storage_service,
                                        ota_receive_service,
                                        narrowband_service,
                                        downlink_service,
                                        log_service,
                                        command_interface);
    Log(LogLevel::Info, "service loop exit " + TelemetryManager::BuildSummary(final_snapshot));

    return (final_snapshot.lifecycle_state == ServiceLifecycleState::Fatal) ? 4 : 0;
}

}  // namespace youyeetoo
