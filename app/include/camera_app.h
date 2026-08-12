#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace youyeetoo {

struct CameraInfo {
    std::string serial_number;
    std::string camera_name;
    std::string firmware_version;
    // Stable SDK model identity. Do not use camera_name as a routing key.
    std::string model_key;
    // Comma-separated operations known to be supported by this SDK model.
    std::string capabilities;
};

struct BatteryInfo {
    std::string power_type;
    uint32_t battery_level = 0;
    uint32_t battery_scale = 0;
};

struct StorageInfo {
    std::string state;
    uint64_t free_space = 0;
    uint64_t total_space = 0;
};

enum class CameraCommand {
    Service,
};

struct CameraRuntimeStatus {
    bool connected = false;
    bool capturing = false;
    bool battery_ok = false;
    bool storage_ok = false;
    bool media_time_ok = false;
    BatteryInfo battery;
    StorageInfo storage;
    int64_t media_time = -1;
    std::string recent_error;
};

struct CameraAppOptions {
    bool debug = false;
    int timeout_ms = 10000;
    int poll_interval_ms = 2000;
    int loop_interval_ms = 200;
    int max_loops = 0;
    CameraCommand command = CameraCommand::Service;
    std::string log_path;
    std::string serial_number;
    std::string output_dir;
    std::string telemetry_command_defs = "/opt/youyeetoo_app/configs/telemetry_commands.txt";
    std::string telemetry_command_inbox = "/var/opt/youyeetoo/runtime/telemetry_command_request.txt";
    // CAM-003: 端口→槽位映射配置文件路径，空字符串表示不启用槽位解析
    std::string port_map_path = "/opt/youyeetoo_app/configs/camera_port_map.txt";
    // CAN 服务配置文件路径；所有 CAN 参数（接口名、CAN ID、波特率说明等）从此文件加载
    std::string can_config_path = "/opt/youyeetoo_app/configs/can_config.txt";
};

class CameraApp {
public:
    int Run(const CameraAppOptions& options);
};

}  // namespace youyeetoo
