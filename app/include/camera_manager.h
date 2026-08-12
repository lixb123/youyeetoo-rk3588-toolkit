#pragma once

#include <map>
#include <memory>
#include <string>

#include "camera_app.h"
#include "camera_controller.h"
#include "camera_interface.h"
#include "camera_port_resolver.h"
#include "camera_sdk_adapter.h"
#include "user_data_store.h"

namespace youyeetoo {

class CameraManager : public ICameraManager {
public:
    CameraManager();

    void Configure(const CameraAppOptions& options);

    // ICameraManager interface
    void Tick()                                       override;
    bool StartTask(const CameraTaskRequest& request)  override;
    bool HasActiveTask()                        const override;
    bool IsTaskFinished()                       const override;
    const CameraControllerSnapshot& snapshot()  const override;

    CameraSdkAdapter* adapter();

private:
    struct CameraSession {
        explicit CameraSession(const std::string& serial = std::string());

        std::string target_serial;
        std::unique_ptr<CameraSdkAdapter> adapter;
        std::unique_ptr<FileSystemUserDataStore> user_data_store;
        std::unique_ptr<CameraController> controller;
    };

    CameraSession* DefaultSession();
    const CameraSession* DefaultSession() const;
    CameraSession* GetOrCreateSession(const std::string& target_serial);
    const CameraSession* FindSessionByActualSerial(const std::string& actual_serial) const;
    CameraSession* FindSessionByActualSerial(const std::string& actual_serial);
    std::string ResolveTargetSerial(const CameraTaskRequest& request) const;
    void ConfigureSession(CameraSession* session);
    void AdjustRequestForSession(CameraTaskRequest* request, const CameraSession& session) const;

    // CAM-003: 将 cam0/cam1/... 槽位名解析为真实序列号
    // 返回 true 表示找到有效序列号，false 表示槽位未配置或相机未接入
    bool ResolveSlotName(const std::string& slot_name,
                         std::string* resolved_serial) const;

    CameraAppOptions options_;
    CameraControllerSnapshot empty_snapshot_;
    std::unique_ptr<CameraSession> default_session_;
    std::map<std::string, std::unique_ptr<CameraSession>> named_sessions_;
    std::string last_active_session_key_;

    // CAM-003: 端口映射表，由 Configure() 加载，支持热重载（Tick 可定期刷新）
    // slot_to_serial_: {cam0 → IAHEA...}，serial 为空表示该槽位相机未接入
    // serial_to_slot_: 反向表，用于遥测时把序列号转回槽位名
    std::map<std::string, std::string> slot_to_serial_;
    std::map<std::string, std::string> serial_to_slot_;
};

}  // namespace youyeetoo
