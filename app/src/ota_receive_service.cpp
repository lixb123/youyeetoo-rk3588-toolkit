#include "ota_receive_service.h"

namespace youyeetoo {

void OtaReceiveService::Configure(const CameraAppOptions&) {}

void OtaReceiveService::Tick() {}

bool OtaReceiveService::HandleModuleTask(const ModuleTask& task, std::string* result_message) {
    status_summary_ = "ota_rx=placeholder last_task=" + task.task_name;
    if (result_message != nullptr) {
        *result_message = status_summary_;
    }
    return true;
}

const std::string& OtaReceiveService::StatusSummary() const {
    return status_summary_;
}

}  // namespace youyeetoo
