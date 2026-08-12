#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <sys/types.h>

#include "camera_app.h"

namespace youyeetoo {

struct WorkerDescriptor {
    std::string name;
    // Entry function executed inside the child process.
    // Receives the child-side fd of the IPC pair and program options.
    // Must loop until the fd is broken (parent died / graceful shutdown).
    std::function<void(int child_fd, const CameraAppOptions&)> child_main;
};

// ─── WorkerSupervisor ────────────────────────────────────────────────────────
// Manages a set of forked worker processes:
//   • Register() + StartAll() to spawn workers at boot.
//   • Tick()  called from the main loop: reaps dead children, restarts them
//             with exponential back-off (1 s → 2 s → 4 s → … → 30 s cap).
//   • GetParentFd() returns the IPC fd for communication with a live worker;
//     proxies always call this fresh (supervisor is the single fd owner).
// ─────────────────────────────────────────────────────────────────────────────
class WorkerSupervisor {
public:
    void Register(const WorkerDescriptor& desc);
    void StartAll(const CameraAppOptions& options);

    // Call once per main-loop iteration.
    void Tick(const CameraAppOptions& options);

    // Returns the parent-side IPC fd, or -1 if worker is not running.
    // Caller MUST NOT close this fd (supervisor owns it).
    int  GetParentFd(const std::string& name) const;
    bool IsRunning(const std::string& name)   const;
    int  RestartCount(const std::string& name) const;

private:
    struct WorkerState {
        WorkerDescriptor desc;
        pid_t  pid        = -1;
        int    parent_fd  = -1;
        int    restart_count = 0;
        bool   pending_restart = false;
        std::chrono::steady_clock::time_point next_restart_at;
    };

    void SpawnOne(WorkerState& ws, const CameraAppOptions& options);
    static int BackoffSeconds(int restart_count);

    std::unordered_map<std::string, WorkerState> workers_;
    std::vector<std::string> order_;  // insertion order for deterministic startup
};

}  // namespace youyeetoo
