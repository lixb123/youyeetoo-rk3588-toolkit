#include "telemetry_command_interface.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include "user_data_store.h"

namespace youyeetoo {
namespace {

using DefinitionMap = std::map<std::string, TelemetryCommandInterface::CommandDefinition>;
namespace fs = std::filesystem;

std::vector<std::string> Split(const std::string& line, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, delimiter)) {
        parts.push_back(item);
    }
    return parts;
}

std::vector<std::string> SplitWhitespace(const std::string& line) {
    std::vector<std::string> parts;
    std::stringstream ss(line);
    std::string item;
    while (ss >> item) {
        parts.push_back(item);
    }
    return parts;
}

DefinitionMap& Definitions() {
    static DefinitionMap definitions;
    return definitions;
}

}  // namespace

void TelemetryCommandInterface::Configure(const CameraAppOptions& options) {
    options_ = options;
    last_command_name_.clear();
    last_command_result_.clear();
    last_routed_module_.clear();

    std::error_code ec;
    const fs::path inbox_path(options_.telemetry_command_inbox);
    if (inbox_path.has_parent_path()) {
        fs::create_directories(inbox_path.parent_path(), ec);
    }
}

void TelemetryCommandInterface::Tick(CommandDispatcher* dispatcher, ServiceContext* service_context) {
    if (dispatcher == nullptr || service_context == nullptr) {
        last_command_result_ = "telemetry command interface has no dispatcher or service context";
        return;
    }

    ProcessCommandFile(dispatcher, service_context);
}

const std::string& TelemetryCommandInterface::last_command_name() const {
    return last_command_name_;
}

const std::string& TelemetryCommandInterface::last_command_result() const {
    return last_command_result_;
}

const std::string& TelemetryCommandInterface::last_routed_module() const {
    return last_routed_module_;
}

bool TelemetryCommandInterface::ProcessCommandFile(CommandDispatcher* dispatcher,
                                                   ServiceContext* service_context) {
    std::string error_message;
    if (!LoadDefinitions(&error_message)) {
        last_command_result_ = error_message;
        return false;
    }

    std::ifstream input(options_.telemetry_command_inbox);
    if (!input) {
        return false;
    }

    std::vector<std::string> commands;
    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = Trim(line);
        if (!trimmed.empty()) {
            commands.push_back(trimmed);
        }
    }
    input.close();

    if (commands.empty()) {
        return false;
    }

    std::string command_name;
    std::map<std::string, std::string> runtime_parameters;
    if (!ParseCommandLine(commands.front(), &command_name, &runtime_parameters, &error_message)) {
        last_command_name_ = commands.front();
        last_routed_module_.clear();
        last_command_result_ = error_message;
        std::cerr << "[TelemetryCommandInterface] parse failed command="
                  << commands.front() << " error=" << error_message << "\n";
        return false;
    }
    commands.erase(commands.begin());

    std::ofstream rewrite(options_.telemetry_command_inbox, std::ios::trunc);
    for (const auto& remaining : commands) {
        rewrite << remaining << "\n";
    }

    const auto it = Definitions().find(command_name);
    last_command_name_ = command_name;
    if (it == Definitions().end()) {
        last_command_result_ = "unknown telemetry command: " + command_name;
        last_routed_module_.clear();
        std::cerr << "[TelemetryCommandInterface] " << last_command_result_ << "\n";
        return false;
    }

    CommandRequest request = BuildCommandRequest(it->second, runtime_parameters);
    if (!ResolveCommandRequest(&request, &error_message)) {
        last_routed_module_.clear();
        last_command_result_ = error_message;
        std::cerr << "[TelemetryCommandInterface] resolve failed command="
                  << command_name << " error=" << error_message << "\n";
        return false;
    }

    CommandDispatchResult dispatch_result;
    if (!dispatcher->Dispatch(request, service_context, &dispatch_result)) {
        last_routed_module_ = dispatch_result.routed_module;
        last_command_result_ =
            "dispatch_failed error=" + dispatch_result.error_code +
            " summary=" + dispatch_result.summary;
        std::cerr << "[TelemetryCommandInterface] " << last_command_result_
                  << " command=" << command_name << "\n";
        return false;
    }

    last_routed_module_ = dispatch_result.routed_module;
    last_command_result_ =
        "dispatch_ok mode=" + CommandDispatcher::ModeToString(dispatch_result.mode) +
        " routed_to=" + dispatch_result.routed_module +
        " summary=" + dispatch_result.summary;
    return true;
}

bool TelemetryCommandInterface::LoadDefinitions(std::string* error_message) {
    std::ifstream input(options_.telemetry_command_defs);
    std::string opened_path = options_.telemetry_command_defs;
    if (!input && options_.telemetry_command_defs != "configs/telemetry_commands.txt") {
        input.open("configs/telemetry_commands.txt");
        opened_path = "configs/telemetry_commands.txt";
    }
    if (!input) {
        if (error_message != nullptr) {
            *error_message = "failed to open telemetry command definitions: " +
                             options_.telemetry_command_defs;
        }
        return false;
    }

    DefinitionMap parsed_definitions;
    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        CommandDefinition definition;
        std::string line_error;
        if (!ParseDefinitionLine(trimmed, &definition, &line_error)) {
            if (error_message != nullptr) {
                *error_message = "definition parse error line " + std::to_string(line_number) +
                                 ": " + line_error;
            }
            return false;
        }
        parsed_definitions[definition.command_name] = definition;
    }

    Definitions() = parsed_definitions;
    if (last_command_result_.empty()) {
        last_command_result_ = "loaded telemetry command definitions from " + opened_path;
    }
    return true;
}

CommandRequest TelemetryCommandInterface::BuildCommandRequest(
    const CommandDefinition& definition,
    const std::map<std::string, std::string>& runtime_parameters) {
    CommandRequest request;
    request.command_id = NextCommandId();
    request.source = CommandSource::TelemetryFile;
    request.category = definition.category;
    request.command_name = definition.command_name;
    request.task_name = definition.task_name;
    request.repeat_count = definition.repeat_count;
    request.output_dir = definition.output_dir;
    request.remote_path = definition.remote_path;
    request.local_path = definition.local_path;
    request.delete_confirmed = definition.delete_confirmed;
    request.parameters = runtime_parameters;
    return request;
}

bool TelemetryCommandInterface::ResolveCommandRequest(CommandRequest* request,
                                                      std::string* error_message) const {
    if (request == nullptr) {
        if (error_message != nullptr) {
            *error_message = "command request is null";
        }
        return false;
    }

    std::string target_camera_serial;
    const auto camera_serial_it = request->parameters.find("camera_serial");
    if (camera_serial_it != request->parameters.end()) {
        target_camera_serial = camera_serial_it->second;
    }

    const auto remote_path_it = request->parameters.find("remote_path");
    if (remote_path_it != request->parameters.end() && !remote_path_it->second.empty()) {
        request->remote_path = remote_path_it->second;
    }

    const auto local_path_it = request->parameters.find("local_path");
    if (local_path_it != request->parameters.end() && !local_path_it->second.empty()) {
        request->local_path = local_path_it->second;
    }

    const auto output_dir_it = request->parameters.find("output_dir");
    if (output_dir_it != request->parameters.end() && !output_dir_it->second.empty()) {
        request->output_dir = output_dir_it->second;
    }

    if (!request->remote_path.empty() && request->remote_path[0] == '@') {
        std::string resolved_path;
        if (!ResolveLatestMediaToken(request->remote_path,
                                     target_camera_serial,
                                     &resolved_path,
                                     error_message)) {
            return false;
        }
        request->remote_path = resolved_path;
    }

    return true;
}

bool TelemetryCommandInterface::ParseCommandLine(
    const std::string& line,
    std::string* command_name,
    std::map<std::string, std::string>* runtime_parameters,
    std::string* error_message) {
    if (command_name == nullptr || runtime_parameters == nullptr) {
        if (error_message != nullptr) {
            *error_message = "command line output parameter is null";
        }
        return false;
    }

    const std::vector<std::string> parts = SplitWhitespace(Trim(line));
    if (parts.empty()) {
        if (error_message != nullptr) {
            *error_message = "command line is empty";
        }
        return false;
    }

    *command_name = parts.front();
    runtime_parameters->clear();

    for (std::size_t i = 1; i < parts.size(); ++i) {
        const std::size_t separator = parts[i].find('=');
        if (separator == std::string::npos || separator == 0 || separator + 1 >= parts[i].size()) {
            if (error_message != nullptr) {
                *error_message = "invalid command parameter: " + parts[i];
            }
            return false;
        }

        const std::string key = Trim(parts[i].substr(0, separator));
        const std::string value = Trim(parts[i].substr(separator + 1));
        if (key.empty() || value.empty()) {
            if (error_message != nullptr) {
                *error_message = "command parameter key/value is empty: " + parts[i];
            }
            return false;
        }
        (*runtime_parameters)[key] = value;
    }

    return true;
}

bool TelemetryCommandInterface::ResolveLatestMediaToken(const std::string& token,
                                                        const std::string& camera_serial,
                                                        std::string* resolved_path,
                                                        std::string* error_message) {
    if (resolved_path == nullptr) {
        if (error_message != nullptr) {
            *error_message = "resolved_path is null";
        }
        return false;
    }

    if (token != "@LAST_MEDIA_FIRST") {
        if (error_message != nullptr) {
            *error_message = "unsupported media token: " + token;
        }
        return false;
    }

    const std::string latest_media_list_path = LatestMediaListPath(camera_serial);
    std::ifstream input(latest_media_list_path);
    if (!input) {
        if (error_message != nullptr) {
            *error_message = "latest media list file not found: " + latest_media_list_path;
        }
        return false;
    }

    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        *resolved_path = trimmed;
        return true;
    }

    if (error_message != nullptr) {
        *error_message = "latest media list file contains no media path: " + latest_media_list_path;
    }
    return false;
}

bool TelemetryCommandInterface::ParseDefinitionLine(const std::string& line,
                                                    CommandDefinition* definition,
                                                    std::string* error_message) {
    if (definition == nullptr) {
        if (error_message != nullptr) {
            *error_message = "definition output is null";
        }
        return false;
    }

    const std::vector<std::string> parts = Split(line, '|');
    if (parts.size() != 8) {
        if (error_message != nullptr) {
            *error_message = "expected 8 pipe-separated fields";
        }
        return false;
    }

    definition->command_name = Trim(parts[0]);
    definition->category = ParseCategory(Trim(parts[1]));
    definition->task_name = Trim(parts[2]);
    if (definition->command_name.empty() ||
        definition->category == CommandCategory::Unknown ||
        definition->task_name.empty()) {
        if (error_message != nullptr) {
            *error_message = "command name, category or task name is invalid";
        }
        return false;
    }

    try {
        definition->repeat_count = std::stoi(Trim(parts[3]));
    } catch (const std::exception&) {
        if (error_message != nullptr) {
            *error_message = "repeat_count must be integer";
        }
        return false;
    }

    definition->output_dir = Trim(parts[4]);
    definition->remote_path = Trim(parts[5]);
    definition->local_path = Trim(parts[6]);
    if (!ParseBool(Trim(parts[7]), &definition->delete_confirmed)) {
        if (error_message != nullptr) {
            *error_message = "delete_confirmed must be true/false";
        }
        return false;
    }
    return true;
}

CommandCategory TelemetryCommandInterface::ParseCategory(const std::string& value) {
    if (value == "system") {
        return CommandCategory::System;
    }
    if (value == "boot") {
        return CommandCategory::Boot;
    }
    if (value == "usb") {
        return CommandCategory::Usb;
    }
    if (value == "camera") {
        return CommandCategory::Camera;
    }
    if (value == "storage") {
        return CommandCategory::Storage;
    }
    if (value == "narrowband") {
        return CommandCategory::Narrowband;
    }
    if (value == "ota_receive") {
        return CommandCategory::OtaReceive;
    }
    if (value == "downlink") {
        return CommandCategory::Downlink;
    }
    if (value == "log") {
        return CommandCategory::Log;
    }
    return CommandCategory::Unknown;
}

std::string TelemetryCommandInterface::LatestMediaListPath(const std::string& camera_serial) {
    return FileSystemUserDataStore::BuildRuntimePath("camera_last_media_list",
                                                     ".txt",
                                                     camera_serial);
}

std::string TelemetryCommandInterface::Trim(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool TelemetryCommandInterface::ParseBool(const std::string& value, bool* output) {
    if (output == nullptr) {
        return false;
    }
    if (value == "true" || value == "1" || value == "yes") {
        *output = true;
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value.empty()) {
        *output = false;
        return true;
    }
    return false;
}

int TelemetryCommandInterface::NextCommandId() {
    static std::atomic<int> next_id{1};
    return next_id.fetch_add(1);
}

}  // namespace youyeetoo
