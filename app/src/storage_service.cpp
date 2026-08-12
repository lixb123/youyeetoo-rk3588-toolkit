#include "storage_service.h"

namespace youyeetoo {

void StorageService::Configure(const CameraAppOptions&) {}

void StorageService::Tick() {}

bool StorageService::HandleModuleTask(const ModuleTask& task, std::string* result_message) {
    status_summary_ = "storage=placeholder last_task=" + task.task_name;
    if (result_message != nullptr) {
        *result_message = status_summary_;
    }
    return true;
}

const std::string& StorageService::StatusSummary() const {
    return status_summary_;
}

}  // namespace youyeetoo
