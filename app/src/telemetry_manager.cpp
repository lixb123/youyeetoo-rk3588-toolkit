#include "telemetry_manager.h"

#include <sstream>

#include "camera_controller.h"

namespace youyeetoo {
namespace {

std::string BoolToYesNo(bool value) {
    return value ? "yes" : "no";
}

std::string BuildCameraStatusSummary(const CameraRuntimeStatus& status) {
    std::ostringstream oss;
    oss << "connected=" << BoolToYesNo(status.connected)
        << " capturing=" << BoolToYesNo(status.capturing);

    if (status.battery_ok) {
        oss << " battery=" << status.battery.battery_level << "/" << status.battery.battery_scale;
    } else {
        oss << " battery=unavailable";
    }

    if (status.storage_ok) {
        oss << " storage=" << status.storage.state
            << " free=" << status.storage.free_space
            << " total=" << status.storage.total_space;
    } else {
        oss << " storage=unavailable";
    }

    if (status.media_time_ok) {
        oss << " media_time=" << status.media_time;
    } else {
        oss << " media_time=unavailable";
    }

    if (!status.recent_error.empty()) {
        oss << " recent_error=" << status.recent_error;
    }
    return oss.str();
}

}  // namespace

MainServiceSnapshot TelemetryManager::BuildSnapshot(int loop_count,
                                                   const CameraControllerSnapshot& camera_snapshot,
                                                   const TaskManager& task_manager,
                                                   const BootStateService& boot_state_service,
                                                   const CanService& can_service,
                                                   const UsbService& usb_service,
                                                   const StorageService& storage_service,
                                                   const OtaReceiveService& ota_receive_service,
                                                   const NarrowbandService& narrowband_service,
                                                   const DownlinkService& downlink_service,
                                                   const LogService& log_service,
                                                   const TelemetryCommandInterface& command_interface) const {
    MainServiceSnapshot snapshot;
    snapshot.loop_count = loop_count;
    snapshot.task_active = task_manager.HasActiveTask();
    snapshot.completed_task_count = task_manager.completed_task_count();
    snapshot.last_error = task_manager.last_error();
    snapshot.last_task_summary = task_manager.last_task_summary();
    snapshot.last_command_name = command_interface.last_command_name();
    snapshot.last_command_result = command_interface.last_command_result();
    snapshot.last_routed_module = command_interface.last_routed_module();
    snapshot.boot_state_summary = boot_state_service.StatusSummary();
    snapshot.can_summary = can_service.StatusSummary();
    snapshot.usb_summary = usb_service.StatusSummary();
    snapshot.storage_summary = storage_service.StatusSummary();
    snapshot.ota_receive_summary = ota_receive_service.StatusSummary();
    snapshot.narrowband_summary = narrowband_service.StatusSummary();
    snapshot.downlink_summary = downlink_service.StatusSummary();
    snapshot.log_summary = log_service.StatusSummary();
    snapshot.camera_snapshot = camera_snapshot;
    snapshot.lifecycle_state = EvaluateLifecycle(camera_snapshot, task_manager);
    snapshot.healthy = EvaluateHealth(snapshot.lifecycle_state);
    return snapshot;
}

std::string TelemetryManager::LifecycleStateToString(ServiceLifecycleState state) {
    switch (state) {
    case ServiceLifecycleState::Starting:
        return "STARTING";
    case ServiceLifecycleState::Running:
        return "RUNNING";
    case ServiceLifecycleState::Degraded:
        return "DEGRADED";
    case ServiceLifecycleState::Stopped:
        return "STOPPED";
    case ServiceLifecycleState::Fatal:
        return "FATAL";
    }
    return "STARTING";
}

std::string TelemetryManager::BuildSummary(const MainServiceSnapshot& snapshot) {
    std::ostringstream oss;
    oss << "profile=BOARD_CONTROLLER"
        << " lifecycle=" << LifecycleStateToString(snapshot.lifecycle_state)
        << " healthy=" << BoolToYesNo(snapshot.healthy)
        << " loop=" << snapshot.loop_count
        << " task_active=" << BoolToYesNo(snapshot.task_active)
        << " completed_tasks=" << snapshot.completed_task_count
        << " camera_device=" << CameraController::DeviceStateToString(snapshot.camera_snapshot.device_state)
        << " camera_task=" << CameraController::TaskTypeToString(snapshot.camera_snapshot.task_type)
        << "/" << CameraController::TaskStateToString(snapshot.camera_snapshot.task_state)
        << " camera_status{" << BuildCameraStatusSummary(snapshot.camera_snapshot.runtime_status) << "}";

    if (!snapshot.last_task_summary.empty()) {
        oss << " last_task_summary=" << snapshot.last_task_summary;
    }
    if (!snapshot.last_error.empty()) {
        oss << " last_error=" << snapshot.last_error;
    }
    if (!snapshot.last_command_name.empty()) {
        oss << " last_command=" << snapshot.last_command_name;
    }
    if (!snapshot.last_command_result.empty()) {
        oss << " last_command_result=" << snapshot.last_command_result;
    }
    if (!snapshot.last_routed_module.empty()) {
        oss << " last_routed_module=" << snapshot.last_routed_module;
    }
    if (!snapshot.camera_snapshot.error_message.empty()) {
        oss << " camera_error=" << snapshot.camera_snapshot.error_message;
    }
    if (!snapshot.boot_state_summary.empty()) {
        oss << " " << snapshot.boot_state_summary;
    }
    if (!snapshot.can_summary.empty()) {
        oss << " " << snapshot.can_summary;
    }
    if (!snapshot.usb_summary.empty()) {
        oss << " " << snapshot.usb_summary;
    }
    if (!snapshot.storage_summary.empty()) {
        oss << " " << snapshot.storage_summary;
    }
    if (!snapshot.ota_receive_summary.empty()) {
        oss << " " << snapshot.ota_receive_summary;
    }
    if (!snapshot.narrowband_summary.empty()) {
        oss << " " << snapshot.narrowband_summary;
    }
    if (!snapshot.downlink_summary.empty()) {
        oss << " " << snapshot.downlink_summary;
    }
    if (!snapshot.log_summary.empty()) {
        oss << " " << snapshot.log_summary;
    }
    return oss.str();
}

ServiceLifecycleState TelemetryManager::EvaluateLifecycle(const CameraControllerSnapshot& camera_snapshot,
                                                          const TaskManager& task_manager) {
    if (camera_snapshot.device_state == CameraDeviceState::Fatal) {
        return ServiceLifecycleState::Fatal;
    }

    if (!task_manager.HasBootstrapped()) {
        return ServiceLifecycleState::Starting;
    }

    if (camera_snapshot.device_state == CameraDeviceState::Unavailable ||
        camera_snapshot.device_state == CameraDeviceState::Connecting ||
        camera_snapshot.device_state == CameraDeviceState::ErrorRecover) {
        return ServiceLifecycleState::Degraded;
    }

    return ServiceLifecycleState::Running;
}

bool TelemetryManager::EvaluateHealth(ServiceLifecycleState state) {
    return state == ServiceLifecycleState::Running;
}

}  // namespace youyeetoo
