#include "downlink_service.h"

namespace youyeetoo {

void DownlinkService::Configure(const CameraAppOptions&) {}

void DownlinkService::Tick() {}

bool DownlinkService::HandleModuleTask(const ModuleTask& task, std::string* result_message) {
    status_summary_ = "downlink=placeholder last_task=" + task.task_name;
    if (result_message != nullptr) {
        *result_message = status_summary_;
    }
    return true;
}

const std::string& DownlinkService::StatusSummary() const {
    return status_summary_;
}

}  // namespace youyeetoo
