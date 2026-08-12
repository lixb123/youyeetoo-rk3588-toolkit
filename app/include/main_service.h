#pragma once

#include "boot_state_service.h"
#include "camera_app.h"
#include "can_service.h"
#include "command_dispatcher.h"
#include "downlink_service.h"
#include "log_service.h"
#include "narrowband_service.h"
#include "ota_receive_service.h"
#include "service_context.h"
#include "storage_service.h"
#include "task_manager.h"
#include "usb_service.h"

namespace youyeetoo {

class MainServiceApp {
public:
    int Run(const CameraAppOptions& options);
};

}  // namespace youyeetoo
