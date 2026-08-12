#pragma once

#include <map>
#include <string>

#include "camera_app.h"
#include "command_dispatcher.h"
#include "command_types.h"

namespace youyeetoo {

class TelemetryCommandInterface {
public:
    struct CommandDefinition {
        std::string command_name;
        CommandCategory category = CommandCategory::Unknown;
        std::string task_name;
        int repeat_count = 1;
        std::string output_dir;
        std::string remote_path;
        std::string local_path;
        bool delete_confirmed = false;
    };

    void Configure(const CameraAppOptions& options);
    void Tick(CommandDispatcher* dispatcher, ServiceContext* service_context);

    const std::string& last_command_name() const;
    const std::string& last_command_result() const;
    const std::string& last_routed_module() const;

private:
    bool ProcessCommandFile(CommandDispatcher* dispatcher, ServiceContext* service_context);
    bool LoadDefinitions(std::string* error_message);
    static CommandRequest BuildCommandRequest(
        const CommandDefinition& definition,
        const std::map<std::string, std::string>& runtime_parameters);
    bool ResolveCommandRequest(CommandRequest* request, std::string* error_message) const;
    static bool ResolveLatestMediaToken(const std::string& token,
                                        const std::string& camera_serial,
                                        std::string* resolved_path,
                                        std::string* error_message);
    static bool ParseCommandLine(const std::string& line,
                                 std::string* command_name,
                                 std::map<std::string, std::string>* runtime_parameters,
                                 std::string* error_message);

    static bool ParseDefinitionLine(const std::string& line,
                                    CommandDefinition* definition,
                                    std::string* error_message);
    static CommandCategory ParseCategory(const std::string& value);
    static std::string LatestMediaListPath(const std::string& camera_serial);
    static std::string Trim(const std::string& value);
    static bool ParseBool(const std::string& value, bool* output);
    static int NextCommandId();

    CameraAppOptions options_;
    std::string last_command_name_;
    std::string last_command_result_;
    std::string last_routed_module_;
};

}  // namespace youyeetoo
