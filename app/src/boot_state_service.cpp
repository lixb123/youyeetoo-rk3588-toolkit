#include "boot_state_service.h"

namespace youyeetoo {

void BootStateService::Configure(const CameraAppOptions&) {}

void BootStateService::Tick() {}

bool BootStateService::HandleModuleTask(const ModuleTask& task, std::string* result_message) {
    status_summary_ = "boot_state=placeholder last_task=" + task.task_name;
    if (result_message != nullptr) {
        *result_message = status_summary_;
    }
    return true;
}

const std::string& BootStateService::StatusSummary() const {
    return status_summary_;
}

}  // namespace youyeetoo
