#pragma once

#include <string>

#include "camera_app.h"

namespace youyeetoo {

class WorkerSupervisor;

// ─── Child-process entry ─────────────────────────────────────────────────────
// Runs inside telemetry_worker subprocess.
// Collects system-level telemetry (CPU, memory, temperature, storage) and
// sends it to the parent periodically.
// ─────────────────────────────────────────────────────────────────────────────
void TelemetryWorkerChildMain(int child_fd, const CameraAppOptions& options);

struct SystemTelemetry {
    double  cpu_usage_pct  = 0.0;   // 0..100
    uint64_t mem_used_kb   = 0;
    uint64_t mem_total_kb  = 0;
    double  cpu_temp_c     = 0.0;
    double  board_temp_c   = 0.0;
    uint64_t disk_used_kb  = 0;
    uint64_t disk_total_kb = 0;
    // Derived
    double  mem_usage_pct() const {
        return (mem_total_kb > 0) ? 100.0 * mem_used_kb / mem_total_kb : 0.0;
    }
};

// ─── Parent-side proxy ────────────────────────────────────────────────────────
class TelemetryWorkerProxy {
public:
    TelemetryWorkerProxy(WorkerSupervisor* supervisor, std::string worker_name);

    void Tick();

    const SystemTelemetry& system_telemetry() const { return telemetry_; }
    const std::string& StatusSummary() const         { return status_summary_; }
    bool channel_alive() const                       { return channel_alive_;  }

private:
    void ProcessMessage(const std::string& msg);

    WorkerSupervisor* supervisor_;
    std::string       worker_name_;
    SystemTelemetry   telemetry_;
    std::string       status_summary_ = "telemetry=subprocess_starting";
    bool              channel_alive_  = false;
    int               last_seen_fd_   = -1;
};

}  // namespace youyeetoo
