#pragma once

#include <map>
#include <string>

namespace youyeetoo {

enum class CommandSource {
    TelemetryFile,
    CanBus,
    Maintenance,
};

enum class CommandCategory {
    Unknown,
    System,
    Boot,
    Usb,
    Camera,
    Storage,
    Narrowband,
    OtaReceive,
    Downlink,
    Log,
};

enum class CommandExecutionMode {
    Rejected,
    Immediate,
    ModuleTask,
};

struct CommandRequest {
    int command_id = 0;
    CommandSource source = CommandSource::TelemetryFile;
    CommandCategory category = CommandCategory::Unknown;
    std::string command_name;
    std::string task_name;
    int repeat_count = 1;
    std::string output_dir;
    std::string remote_path;
    std::string local_path;
    bool delete_confirmed = false;
    std::map<std::string, std::string> parameters;
};

struct ModuleTask {
    CommandCategory category = CommandCategory::Unknown;
    std::string module_name;
    std::string task_name;
    int repeat_count = 1;
    std::string output_dir;
    std::string remote_path;
    std::string local_path;
    bool delete_confirmed = false;
    std::map<std::string, std::string> parameters;
};

struct CommandDispatchResult {
    bool accepted = false;
    CommandExecutionMode mode = CommandExecutionMode::Rejected;
    std::string routed_module;
    std::string summary;
    std::string error_code;
    ModuleTask module_task;
};

}  // namespace youyeetoo
