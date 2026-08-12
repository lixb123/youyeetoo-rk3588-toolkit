#pragma once

#include <string>

#include "command_types.h"
#include "service_context.h"

namespace youyeetoo {

class CommandDispatcher {
public:
    bool Dispatch(const CommandRequest& request,
                  ServiceContext* service_context,
                  CommandDispatchResult* result) const;

    static std::string CategoryToString(CommandCategory category);
    static std::string ModeToString(CommandExecutionMode mode);

private:
    bool DispatchSystemCommand(const CommandRequest& request,
                               ServiceContext* service_context,
                               CommandDispatchResult* result) const;
    bool DispatchCameraCommand(const CommandRequest& request,
                               ServiceContext* service_context,
                               CommandDispatchResult* result) const;
    bool DispatchServiceModuleTask(const CommandRequest& request,
                                   const std::string& module_name,
                                   ServiceContext* service_context,
                                   CommandDispatchResult* result) const;
    static ModuleTask BuildModuleTask(const CommandRequest& request, const std::string& module_name);
};

}  // namespace youyeetoo
