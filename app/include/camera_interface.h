#pragma once

#include "camera_controller.h"

namespace youyeetoo {

// ─── ICameraManager ──────────────────────────────────────────────────────────
// Pure-virtual interface shared by CameraManager (in-process) and
// CameraWorkerProxy (subprocess). TaskManager depends only on this interface.
// ─────────────────────────────────────────────────────────────────────────────
class ICameraManager {
public:
    virtual ~ICameraManager() = default;

    virtual void Tick() = 0;
    virtual bool StartTask(const CameraTaskRequest& request) = 0;
    virtual bool HasActiveTask() const = 0;
    virtual bool IsTaskFinished() const = 0;
    virtual const CameraControllerSnapshot& snapshot() const = 0;
};

}  // namespace youyeetoo
