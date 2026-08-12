#include "can_service.h"

namespace youyeetoo {

void CanService::Configure(const CameraAppOptions&) {}

void CanService::Tick() {}

bool CanService::HandleModuleTask(const ModuleTask& task, std::string* result_message) {
    status_summary_ = "can=placeholder last_task=" + task.task_name;
    if (result_message != nullptr) {
        *result_message = status_summary_;
    }
    return true;
}

const std::string& CanService::StatusSummary() const {
    return status_summary_;
}

}  // namespace youyeetoo
