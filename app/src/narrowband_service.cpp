#include "narrowband_service.h"

namespace youyeetoo {

void NarrowbandService::Configure(const CameraAppOptions&) {}

void NarrowbandService::Tick() {}

bool NarrowbandService::HandleModuleTask(const ModuleTask& task, std::string* result_message) {
    status_summary_ = "narrowband=placeholder last_task=" + task.task_name;
    if (result_message != nullptr) {
        *result_message = status_summary_;
    }
    return true;
}

const std::string& NarrowbandService::StatusSummary() const {
    return status_summary_;
}

}  // namespace youyeetoo
