#pragma once

namespace youyeetoo {

class BootStateService;
class CanService;
class DownlinkService;
class LogService;
class NarrowbandService;
class OtaReceiveService;
class StorageService;
class TaskManager;
class UsbService;

struct ServiceContext {
    TaskManager* task_manager = nullptr;
    BootStateService* boot_state_service = nullptr;
    CanService* can_service = nullptr;
    UsbService* usb_service = nullptr;
    StorageService* storage_service = nullptr;
    OtaReceiveService* ota_receive_service = nullptr;
    NarrowbandService* narrowband_service = nullptr;
    DownlinkService* downlink_service = nullptr;
    LogService* log_service = nullptr;
};

}  // namespace youyeetoo
