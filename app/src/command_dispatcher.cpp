#include "command_dispatcher.h"

#include "boot_state_service.h"
#include "can_service.h"
#include "downlink_service.h"
#include "log_service.h"
#include "narrowband_service.h"
#include "ota_receive_service.h"
#include "storage_service.h"
#include "task_manager.h"
#include "usb_service.h"

namespace youyeetoo {
bool CommandDispatcher::Dispatch(const CommandRequest& request,
                                 ServiceContext* service_context,
                                 CommandDispatchResult* result) const {
    if (result == nullptr) {
        return false;
    }

    *result = CommandDispatchResult{};
    if (service_context == nullptr || service_context->task_manager == nullptr) {
        result->error_code = "NO_SERVICE_CONTEXT";
        result->summary = "command dispatcher has no service context";
        return false;
    }

    switch (request.category) {
    case CommandCategory::System:
        return DispatchSystemCommand(request, service_context, result);
    case CommandCategory::Boot:
        return DispatchServiceModuleTask(request, "boot_state_service", service_context, result);
    case CommandCategory::Usb:
        return DispatchServiceModuleTask(request, "usb_service", service_context, result);
    case CommandCategory::Camera:
        return DispatchCameraCommand(request, service_context, result);
    case CommandCategory::Storage:
        return DispatchServiceModuleTask(request, "storage_service", service_context, result);
    case CommandCategory::Narrowband:
        return DispatchServiceModuleTask(request, "narrowband_service", service_context, result);
    case CommandCategory::OtaReceive:
        return DispatchServiceModuleTask(request, "ota_receive_service", service_context, result);
    case CommandCategory::Downlink:
        return DispatchServiceModuleTask(request, "downlink_service", service_context, result);
    case CommandCategory::Log:
        return DispatchServiceModuleTask(request, "log_service", service_context, result);
    case CommandCategory::Unknown:
        result->error_code = "UNKNOWN_CATEGORY";
        result->summary = "command category is unknown";
        return false;
    }

    result->error_code = "UNREACHABLE";
    result->summary = "command category dispatch fell through";
    return false;
}

std::string CommandDispatcher::CategoryToString(CommandCategory category) {
    switch (category) {
    case CommandCategory::Unknown:
        return "unknown";
    case CommandCategory::System:
        return "system";
    case CommandCategory::Boot:
        return "boot";
    case CommandCategory::Usb:
        return "usb";
    case CommandCategory::Camera:
        return "camera";
    case CommandCategory::Storage:
        return "storage";
    case CommandCategory::Narrowband:
        return "narrowband";
    case CommandCategory::OtaReceive:
        return "ota_receive";
    case CommandCategory::Downlink:
        return "downlink";
    case CommandCategory::Log:
        return "log";
    }
    return "unknown";
}

std::string CommandDispatcher::ModeToString(CommandExecutionMode mode) {
    switch (mode) {
    case CommandExecutionMode::Rejected:
        return "rejected";
    case CommandExecutionMode::Immediate:
        return "immediate";
    case CommandExecutionMode::ModuleTask:
        return "module_task";
    }
    return "rejected";
}

bool CommandDispatcher::DispatchSystemCommand(const CommandRequest& request,
                                              ServiceContext* service_context,
                                              CommandDispatchResult* result) const {
    result->routed_module = "main_service";
    result->accepted = true;
    result->mode = CommandExecutionMode::Immediate;
    result->summary = "system command accepted: " + request.command_name;
    service_context->task_manager->SetInterfaceStatus(result->summary);
    return true;
}

bool CommandDispatcher::DispatchCameraCommand(const CommandRequest& request,
                                              ServiceContext* service_context,
                                              CommandDispatchResult* result) const {
    if (request.task_name == "camera.init" ||
        request.task_name == "camera.get_status" ||
        request.task_name == "camera.get_capture_status" ||
        request.task_name == "camera.get_battery" ||
        request.task_name == "camera.get_storage" ||
        request.task_name == "camera.take_photo_download" ||
        request.task_name == "camera.set_mode" ||
        request.task_name == "camera.set_param" ||
        request.task_name == "camera.get_param" ||
        request.task_name == "camera.get_media_time" ||
        request.task_name == "camera.get_log" ||
        request.task_name == "camera.list_devices" ||
        request.task_name == "camera.preview.start" ||
        request.task_name == "camera.preview.stop" ||
        request.task_name == "camera.shutdown_maint" ||
        request.task_name == "camera.start_recording" ||
        request.task_name == "camera.stop_recording" ||
        request.task_name == "camera.ext_capture.start" ||
        request.task_name == "camera.ext_capture.stop" ||
        request.task_name == "camera.list_media" ||
        request.task_name == "camera.download_media" ||
        request.task_name == "camera.batch_download" ||
        request.task_name == "camera.delete_media" ||
        request.task_name == "camera.delete_all_media") {
        result->module_task = BuildModuleTask(request, "camera_manager");
        result->routed_module = "camera_manager";
        result->mode = CommandExecutionMode::ModuleTask;

        std::string error_message;
        if (!service_context->task_manager->SubmitModuleTask(result->module_task, &error_message)) {
            result->error_code = "TASK_SUBMIT_FAILED";
            result->summary = error_message;
            return false;
        }

        result->accepted = true;
        result->summary = "camera module task submitted";
        return true;
    }

    result->routed_module = "camera_manager";
    result->error_code = "NOT_IMPLEMENTED";
    result->summary = "camera command reserved but not implemented: " + request.command_name;
    return false;
}

bool CommandDispatcher::DispatchServiceModuleTask(const CommandRequest& request,
                                                  const std::string& module_name,
                                                  ServiceContext* service_context,
                                                  CommandDispatchResult* result) const {
    const ModuleTask task = BuildModuleTask(request, module_name);
    std::string service_result;
    bool accepted = false;

    if (module_name == "boot_state_service" && service_context->boot_state_service != nullptr) {
        accepted = service_context->boot_state_service->HandleModuleTask(task, &service_result);
    } else if (module_name == "usb_service" && service_context->usb_service != nullptr) {
        accepted = service_context->usb_service->HandleModuleTask(task, &service_result);
    } else if (module_name == "storage_service" && service_context->storage_service != nullptr) {
        accepted = service_context->storage_service->HandleModuleTask(task, &service_result);
    } else if (module_name == "narrowband_service" &&
               service_context->narrowband_service != nullptr) {
        accepted = service_context->narrowband_service->HandleModuleTask(task, &service_result);
    } else if (module_name == "ota_receive_service" &&
               service_context->ota_receive_service != nullptr) {
        accepted = service_context->ota_receive_service->HandleModuleTask(task, &service_result);
    } else if (module_name == "downlink_service" && service_context->downlink_service != nullptr) {
        accepted = service_context->downlink_service->HandleModuleTask(task, &service_result);
    } else if (module_name == "log_service" && service_context->log_service != nullptr) {
        accepted = service_context->log_service->HandleModuleTask(task, &service_result);
    } else if (module_name == "can_service" && service_context->can_service != nullptr) {
        accepted = service_context->can_service->HandleModuleTask(task, &service_result);
    } else {
        result->error_code = "MISSING_MODULE";
        result->summary = "missing service module: " + module_name;
        return false;
    }

    result->module_task = task;
    result->routed_module = module_name;
    result->accepted = accepted;
    result->mode = CommandExecutionMode::ModuleTask;
    result->summary = service_result;
    if (!accepted) {
        result->error_code = "MODULE_REJECTED";
    }
    return accepted;
}

ModuleTask CommandDispatcher::BuildModuleTask(const CommandRequest& request,
                                              const std::string& module_name) {
    ModuleTask task;
    task.category = request.category;
    task.module_name = module_name;
    task.task_name = request.task_name;
    task.repeat_count = request.repeat_count;
    task.output_dir = request.output_dir;
    task.remote_path = request.remote_path;
    task.local_path = request.local_path;
    task.delete_confirmed = request.delete_confirmed;
    task.parameters = request.parameters;
    return task;
}

}  // namespace youyeetoo
