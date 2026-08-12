#include "usb_service.h"

namespace youyeetoo {

void UsbService::Configure(const CameraAppOptions&) {}

void UsbService::Tick() {}

bool UsbService::HandleModuleTask(const ModuleTask& task, std::string* result_message) {
    status_summary_ = "usb=placeholder last_task=" + task.task_name;
    if (result_message != nullptr) {
        *result_message = status_summary_;
    }
    return true;
}

const std::string& UsbService::StatusSummary() const {
    return status_summary_;
}

}  // namespace youyeetoo
