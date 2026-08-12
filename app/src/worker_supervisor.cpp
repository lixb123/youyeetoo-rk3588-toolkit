#include "worker_supervisor.h"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>

#include "worker_channel.h"

namespace youyeetoo {

// ─── Registration & startup ──────────────────────────────────────────────────

void WorkerSupervisor::Register(const WorkerDescriptor& desc) {
    WorkerState ws;
    ws.desc = desc;
    workers_[desc.name] = std::move(ws);
    order_.push_back(desc.name);
}

void WorkerSupervisor::StartAll(const CameraAppOptions& options) {
    for (const auto& name : order_) {
        SpawnOne(workers_.at(name), options);
    }
}

// ─── Internal spawn ──────────────────────────────────────────────────────────

void WorkerSupervisor::SpawnOne(WorkerState& ws, const CameraAppOptions& options) {
    int parent_fd = -1, child_fd = -1;
    if (!CreateChannelPair(&parent_fd, &child_fd)) {
        std::cerr << "[Supervisor] channel pair failed for " << ws.desc.name << "\n";
        ws.pending_restart = false;
        return;
    }

    // Close stale parent fd from previous run (child side already gone)
    if (ws.parent_fd >= 0) {
        ::close(ws.parent_fd);
        ws.parent_fd = -1;
    }

    const pid_t pid = ::fork();

    if (pid < 0) {
        std::cerr << "[Supervisor] fork failed: " << std::strerror(errno) << "\n";
        ::close(parent_fd);
        ::close(child_fd);
        return;
    }

    if (pid == 0) {
        // ── Child process ────────────────────────────────────────────────────
        ::close(parent_fd);

        // Close all inherited fds except stdin/stdout/stderr and child_fd.
        // This prevents the child from accidentally holding the sibling workers'
        // parent-side fds open (which would delay EOF detection on restart).
        for (int fd = 3; fd < 1024; ++fd) {
            if (fd != child_fd) ::close(fd);
        }

        ws.desc.child_main(child_fd, options);
        ::close(child_fd);
        std::_Exit(0);  // _Exit: no atexit/static destructors – child is a fork
    }

    // ── Parent process ───────────────────────────────────────────────────────
    ::close(child_fd);
    ws.parent_fd      = parent_fd;
    ws.pid            = pid;
    ws.pending_restart = false;

    std::cout << "[Supervisor] spawned " << ws.desc.name
              << " pid=" << pid
              << " fd=" << parent_fd
              << " restarts=" << ws.restart_count << "\n";
}

// ─── Main-loop tick ──────────────────────────────────────────────────────────

void WorkerSupervisor::Tick(const CameraAppOptions& options) {
    // 1. Reap any dead children.
    while (true) {
        int status = 0;
        const pid_t dead = ::waitpid(-1, &status, WNOHANG);
        if (dead <= 0) break;

        for (auto& [name, ws] : workers_) {
            if (ws.pid != dead) continue;

            if (WIFEXITED(status)) {
                std::cerr << "[Supervisor] " << name
                          << " exited code=" << WEXITSTATUS(status) << "\n";
            } else if (WIFSIGNALED(status)) {
                std::cerr << "[Supervisor] " << name
                          << " killed by signal " << WTERMSIG(status) << "\n";
            }

            ws.pid = -1;
            ++ws.restart_count;
            ws.pending_restart  = true;
            ws.next_restart_at  = std::chrono::steady_clock::now() +
                                  std::chrono::seconds(BackoffSeconds(ws.restart_count));
        }
    }

    // 2. Restart workers whose back-off has elapsed.
    const auto now = std::chrono::steady_clock::now();
    for (const auto& name : order_) {
        auto& ws = workers_.at(name);
        if (!ws.pending_restart)     continue;
        if (now < ws.next_restart_at) continue;

        const int backoff = BackoffSeconds(ws.restart_count - 1);
        std::cout << "[Supervisor] restarting " << name
                  << " (backoff=" << backoff << "s)\n";
        SpawnOne(ws, options);
    }
}

// ─── Queries ─────────────────────────────────────────────────────────────────

int WorkerSupervisor::GetParentFd(const std::string& name) const {
    const auto it = workers_.find(name);
    return (it != workers_.end()) ? it->second.parent_fd : -1;
}

bool WorkerSupervisor::IsRunning(const std::string& name) const {
    const auto it = workers_.find(name);
    return (it != workers_.end()) && (it->second.pid > 0);
}

int WorkerSupervisor::RestartCount(const std::string& name) const {
    const auto it = workers_.find(name);
    return (it != workers_.end()) ? it->second.restart_count : 0;
}

// ─── Back-off schedule ───────────────────────────────────────────────────────

int WorkerSupervisor::BackoffSeconds(int n) {
    // 1 s, 2 s, 4 s, 8 s, 16 s, 30 s (capped)
    if (n <= 0) return 1;
    return std::min(1 << std::min(n, 5), 30);
}

}  // namespace youyeetoo
