#pragma once

#include <string>

#include "camera_app.h"

namespace youyeetoo {

class WorkerSupervisor;

// ─── Child-process entry ─────────────────────────────────────────────────────
// Runs inside can_worker subprocess.
// Owns CAN socket(s), drives send/receive loop.
// ─────────────────────────────────────────────────────────────────────────────
void CanWorkerChildMain(int child_fd, const CameraAppOptions& options);

// ─── Parent-side proxy ────────────────────────────────────────────────────────
// Provides the same StatusSummary() surface as CanService, but reads it from
// the subprocess. Replaces CanService in MainService / TelemetryManager.
// ─────────────────────────────────────────────────────────────────────────────
class CanWorkerProxy {
public:
    CanWorkerProxy(WorkerSupervisor* supervisor, std::string worker_name);

    // Call once per main-loop iteration. Drains status messages from child.
    void Tick();

    // Forward a CAN frame (serialized) to the worker for transmission.
    // Returns false if worker is not running.
    bool SendFrame(const std::string& can_id_hex, const std::string& data_hex);

    const std::string& StatusSummary() const { return status_summary_; }
    bool channel_alive() const               { return channel_alive_;  }

private:
    WorkerSupervisor* supervisor_;
    std::string       worker_name_;
    std::string       status_summary_ = "can=subprocess_starting";
    bool              channel_alive_  = false;
    int               last_seen_fd_   = -1;
};

}  // namespace youyeetoo
