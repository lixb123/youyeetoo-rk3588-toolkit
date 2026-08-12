#pragma once

#include <string>

#include "camera_app.h"
#include "camera_controller.h"
#include "camera_interface.h"

namespace youyeetoo {

class WorkerSupervisor;

// ─── Child-process entry ─────────────────────────────────────────────────────
// Runs inside camera_worker subprocess.
// Owns CameraManager (and therefore CameraSDK).
// Loops until child_fd is broken (parent died or sent shutdown).
// ─────────────────────────────────────────────────────────────────────────────
void CameraWorkerChildMain(int child_fd, const CameraAppOptions& options);

// ─── IPC message serialization (shared by parent and child) ──────────────────
// Protocol: flat text, one "KEY=VALUE" pair per line (newline-terminated).
// Multi-line values: internal '\n' → "\\n", '\\' → "\\\\".
// String fields with prefix "p:" carry CameraTaskRequest::parameters entries.
// ─────────────────────────────────────────────────────────────────────────────
std::string SerializeCameraTaskRequest(const CameraTaskRequest& req);
bool        ParseCameraTaskRequest(const std::string& msg, CameraTaskRequest* out);

std::string SerializeCameraSnapshot(const CameraControllerSnapshot& snap);
bool        ParseCameraSnapshot(const std::string& msg, CameraControllerSnapshot* out);

// ─── Parent-side proxy ────────────────────────────────────────────────────────
// Drop-in ICameraManager implementation that communicates with the child.
// Does not own the IPC fd — the supervisor does.
// Proxies always call supervisor_->GetParentFd() so a restarted worker is
// automatically picked up on the next Tick().
// ─────────────────────────────────────────────────────────────────────────────
class CameraWorkerProxy : public ICameraManager {
public:
    CameraWorkerProxy(WorkerSupervisor* supervisor, std::string worker_name);

    // ICameraManager interface
    void Tick() override;
    bool StartTask(const CameraTaskRequest& request) override;
    bool HasActiveTask()  const override;
    bool IsTaskFinished() const override;
    const CameraControllerSnapshot& snapshot() const override;

    // Extra diagnostics available to main_service
    bool channel_alive()  const { return channel_alive_; }
    int  restart_count()  const;

private:
    void ProcessMessage(const std::string& msg);

    WorkerSupervisor*        supervisor_;
    std::string              worker_name_;
    CameraControllerSnapshot cached_snapshot_;
    bool                     task_active_   = false;
    bool                     task_finished_ = false;
    bool                     channel_alive_ = false;  // false until first snapshot
    int                      last_seen_fd_  = -1;     // detects worker restarts
};

}  // namespace youyeetoo
