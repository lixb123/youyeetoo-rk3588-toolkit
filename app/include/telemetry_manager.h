#pragma once

#include <string>

#include "boot_state_service.h"
#include "can_service.h"
#include "downlink_service.h"
#include "log_service.h"
#include "narrowband_service.h"
#include "ota_receive_service.h"
#include "storage_service.h"
#include "task_manager.h"
#include "telemetry_command_interface.h"
#include "usb_service.h"

namespace youyeetoo {

enum class ServiceLifecycleState {
    Starting,
    Running,
    Degraded,
    Stopped,
    Fatal,
};

struct MainServiceSnapshot {
    ServiceLifecycleState lifecycle_state = ServiceLifecycleState::Starting;
    bool healthy = false;
    bool task_active = false;
    int loop_count = 0;
    int completed_task_count = 0;
    std::string last_error;
    std::string last_task_summary;
    std::string last_command_name;
    std::string last_command_result;
    std::string last_routed_module;
    std::string boot_state_summary;
    std::string can_summary;
    std::string usb_summary;
    std::string storage_summary;
    std::string ota_receive_summary;
    std::string narrowband_summary;
    std::string downlink_summary;
    std::string log_summary;
    CameraControllerSnapshot camera_snapshot;
};

class TelemetryManager {
public:
    MainServiceSnapshot BuildSnapshot(int loop_count,
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
                                     const TelemetryCommandInterface& command_interface) const;

    static std::string LifecycleStateToString(ServiceLifecycleState state);
    static std::string BuildSummary(const MainServiceSnapshot& snapshot);

private:
    static ServiceLifecycleState EvaluateLifecycle(const CameraControllerSnapshot& camera_snapshot,
                                                   const TaskManager& task_manager);
    static bool EvaluateHealth(ServiceLifecycleState state);
};

}  // namespace youyeetoo
