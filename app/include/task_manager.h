#pragma once

#include <string>

#include "camera_interface.h"
#include "command_types.h"

namespace youyeetoo {

class TaskManager {
public:
    // Accepts either CameraManager (in-process) or CameraWorkerProxy (subprocess).
    explicit TaskManager(ICameraManager* camera_manager);

    void Configure(const CameraAppOptions& options);
    bool Bootstrap(std::string* error_message);
    void Tick();
    bool SubmitTask(const CameraTaskRequest& request, std::string* error_message);
    bool SubmitModuleTask(const ModuleTask& task, std::string* error_message);
    void SetInterfaceStatus(const std::string& status);

    bool HasBootstrapped() const;
    bool HasActiveTask() const;
    bool BootstrapTaskFinished() const;
    int completed_task_count() const;
    const std::string& last_task_summary() const;
    const std::string& last_error() const;

private:
    ICameraManager* camera_manager_ = nullptr;
    CameraAppOptions options_;
    bool bootstrapped_ = false;
    bool completion_recorded_ = false;
    int completed_task_count_ = 0;
    std::string last_task_summary_;
    std::string last_error_;
};

}  // namespace youyeetoo
