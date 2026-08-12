#include "log_service.h"

namespace youyeetoo {

void LogService::Configure(const CameraAppOptions&) {}

void LogService::Tick() {}

bool LogService::HandleModuleTask(const ModuleTask& task, std::string* result_message) {
    status_summary_ = "log=placeholder last_task=" + task.task_name;
    if (result_message != nullptr) {
        *result_message = status_summary_;
    }
    return true;
}

const std::string& LogService::StatusSummary() const {
    return status_summary_;
}

}  // namespace youyeetoo
