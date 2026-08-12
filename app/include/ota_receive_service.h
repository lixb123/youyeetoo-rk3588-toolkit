#pragma once

#include <string>

#include "camera_app.h"
#include "command_types.h"

namespace youyeetoo {

class OtaReceiveService {
public:
    void Configure(const CameraAppOptions& options);
    void Tick();
    bool HandleModuleTask(const ModuleTask& task, std::string* result_message);
    const std::string& StatusSummary() const;

private:
    std::string status_summary_ = "ota_rx=placeholder";
};

}  // namespace youyeetoo
