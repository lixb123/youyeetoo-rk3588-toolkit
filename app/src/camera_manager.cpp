#include "camera_manager.h"

#include <algorithm>
#include <iostream>
#include <utility>

namespace youyeetoo {
namespace {

std::string SessionKeyForSerial(const std::string& serial) {
    return serial.empty() ? "__default__" : serial;
}

}  // namespace

CameraManager::CameraSession::CameraSession(const std::string& serial)
    : target_serial(serial),
      adapter(std::make_unique<CameraSdkAdapter>()),
      user_data_store(std::make_unique<FileSystemUserDataStore>(serial)),
      controller(std::make_unique<CameraController>(adapter.get(), user_data_store.get())) {}

CameraManager::CameraManager() : default_session_(std::make_unique<CameraSession>()) {}

void CameraManager::Configure(const CameraAppOptions& options) {
    options_ = options;
    ConfigureSession(DefaultSession());
    for (auto& entry : named_sessions_) {
        ConfigureSession(entry.second.get());
    }

    // CAM-003: 加载端口映射（失败时静默，不阻断服务启动）
    if (!options.port_map_path.empty()) {
        std::string err;
        const auto resolved = CameraPortResolver::Resolve(options.port_map_path, &err);
        if (!err.empty()) {
            std::cerr << "[CameraManager] port_map: " << err << std::endl;
        }
        slot_to_serial_.clear();
        serial_to_slot_.clear();
        for (const auto& [slot, serial] : resolved) {
            slot_to_serial_[slot] = serial;
            if (!serial.empty()) {
                serial_to_slot_[serial] = slot;
            }
        }
        // 打印解析结果，方便上板标定时确认
        for (const auto& [slot, serial] : slot_to_serial_) {
            std::cout << "[CameraManager] port_map: " << slot << " -> "
                      << (serial.empty() ? "(not connected)" : serial) << std::endl;
        }
    }
}

void CameraManager::Tick() {
    if (default_session_ != nullptr && default_session_->controller != nullptr) {
        default_session_->controller->Tick();
    }
    for (auto& entry : named_sessions_) {
        if (entry.second != nullptr && entry.second->controller != nullptr) {
            entry.second->controller->Tick();
        }
    }
}

bool CameraManager::StartTask(const CameraTaskRequest& request) {
    // CAM-003: 如果 target_camera_serial 是槽位名（cam0/cam1/...），解析为真实序列号
    CameraTaskRequest resolved_request = request;
    if (!resolved_request.target_camera_serial.empty() &&
        CameraPortResolver::IsSlotName(resolved_request.target_camera_serial)) {
        std::string actual_serial;
        if (!ResolveSlotName(resolved_request.target_camera_serial, &actual_serial)) {
            std::cerr << "[CameraManager] slot not found or camera not connected: "
                      << resolved_request.target_camera_serial << std::endl;
            return false;
        }
        resolved_request.target_camera_serial = actual_serial;
    }

    const std::string requested_serial = ResolveTargetSerial(resolved_request);
    CameraSession* session = nullptr;

    if (!requested_serial.empty()) {
        session = FindSessionByActualSerial(requested_serial);
        if (session == nullptr &&
            default_session_ != nullptr &&
            default_session_->adapter != nullptr &&
            default_session_->adapter->CurrentSerialNumber() == requested_serial) {
            session = default_session_.get();
        }
    }
    if (session == nullptr) {
        session = GetOrCreateSession(requested_serial);
    }
    if (session == nullptr || session->controller == nullptr) {
        return false;
    }

    CameraTaskRequest adjusted_request = resolved_request;
    AdjustRequestForSession(&adjusted_request, *session);
    if (adjusted_request.type == CameraTaskType::ListDevices) {
        session->controller->SetDeviceListSupplements(CollectKnownCameras());
    }
    last_active_session_key_ = SessionKeyForSerial(session->target_serial);
    return session->controller->StartTask(adjusted_request);
}

bool CameraManager::HasActiveTask() const {
    if (default_session_ != nullptr &&
        default_session_->controller != nullptr &&
        default_session_->controller->HasActiveTask()) {
        return true;
    }
    for (const auto& entry : named_sessions_) {
        if (entry.second != nullptr &&
            entry.second->controller != nullptr &&
            entry.second->controller->HasActiveTask()) {
            return true;
        }
    }
    return false;
}

bool CameraManager::IsTaskFinished() const {
    const std::string active_key =
        last_active_session_key_.empty() ? SessionKeyForSerial(options_.serial_number)
                                         : last_active_session_key_;
    if (active_key == "__default__") {
        return default_session_ != nullptr &&
               default_session_->controller != nullptr &&
               default_session_->controller->IsTaskFinished();
    }

    const auto it = named_sessions_.find(active_key);
    return it != named_sessions_.end() &&
           it->second != nullptr &&
           it->second->controller != nullptr &&
           it->second->controller->IsTaskFinished();
}

const CameraControllerSnapshot& CameraManager::snapshot() const {
    if (!last_active_session_key_.empty()) {
        if (last_active_session_key_ == "__default__") {
            if (default_session_ != nullptr && default_session_->controller != nullptr) {
                return default_session_->controller->snapshot();
            }
        } else {
            const auto it = named_sessions_.find(last_active_session_key_);
            if (it != named_sessions_.end() &&
                it->second != nullptr &&
                it->second->controller != nullptr) {
                return it->second->controller->snapshot();
            }
        }
    }

    if (default_session_ != nullptr && default_session_->controller != nullptr) {
        return default_session_->controller->snapshot();
    }
    return empty_snapshot_;
}

CameraSdkAdapter* CameraManager::adapter() {
    return default_session_ != nullptr ? default_session_->adapter.get() : nullptr;
}

CameraManager::CameraSession* CameraManager::DefaultSession() {
    return default_session_.get();
}

const CameraManager::CameraSession* CameraManager::DefaultSession() const {
    return default_session_.get();
}

CameraManager::CameraSession* CameraManager::GetOrCreateSession(const std::string& target_serial) {
    if (target_serial.empty() ||
        (!options_.serial_number.empty() && target_serial == options_.serial_number)) {
        return DefaultSession();
    }

    const auto key = SessionKeyForSerial(target_serial);
    auto it = named_sessions_.find(key);
    if (it != named_sessions_.end()) {
        return it->second.get();
    }

    auto session = std::make_unique<CameraSession>(target_serial);
    ConfigureSession(session.get());
    auto* session_ptr = session.get();
    named_sessions_.emplace(key, std::move(session));
    return session_ptr;
}

const CameraManager::CameraSession* CameraManager::FindSessionByActualSerial(
    const std::string& actual_serial) const {
    if (actual_serial.empty()) {
        return nullptr;
    }
    if (default_session_ != nullptr &&
        default_session_->adapter != nullptr &&
        default_session_->adapter->CurrentSerialNumber() == actual_serial) {
        return default_session_.get();
    }

    for (const auto& entry : named_sessions_) {
        if (entry.second != nullptr &&
            entry.second->adapter != nullptr &&
            entry.second->adapter->CurrentSerialNumber() == actual_serial) {
            return entry.second.get();
        }
    }
    return nullptr;
}

CameraManager::CameraSession* CameraManager::FindSessionByActualSerial(const std::string& actual_serial) {
    return const_cast<CameraSession*>(
        static_cast<const CameraManager*>(this)->FindSessionByActualSerial(actual_serial));
}

std::string CameraManager::ResolveTargetSerial(const CameraTaskRequest& request) const {
    if (!request.target_camera_serial.empty()) {
        return request.target_camera_serial;
    }
    return options_.serial_number;
}

void CameraManager::ConfigureSession(CameraSession* session) {
    if (session == nullptr || session->adapter == nullptr || session->controller == nullptr) {
        return;
    }

    session->adapter->ConfigureLogging(options_.debug, options_.log_path);
    CameraAppOptions session_options = options_;
    if (!session->target_serial.empty()) {
        session_options.serial_number = session->target_serial;
    }
    session->controller->Configure(session_options);
}

void CameraManager::AdjustRequestForSession(CameraTaskRequest* request,
                                            const CameraSession& session) const {
    if (request == nullptr) {
        return;
    }

    if (!session.target_serial.empty()) {
        request->target_camera_serial = session.target_serial;
    }

    if (session.user_data_store == nullptr || session.target_serial.empty()) {
        return;
    }

    const std::string default_output_dir = "runtime_data/camera";
    const std::string default_batch_output_dir = "runtime_data/camera/batch";
    const std::string session_default_output_dir = session.user_data_store->DefaultOutputDir(options_);
    if (request->output_dir.empty()) {
        request->output_dir = session_default_output_dir;
    } else if (request->output_dir == default_output_dir) {
        request->output_dir = session_default_output_dir;
    } else if (request->output_dir == default_batch_output_dir) {
        request->output_dir = session_default_output_dir + "/batch";
    }
}

std::vector<CameraInfo> CameraManager::CollectKnownCameras() const {
    std::vector<CameraInfo> cameras = CameraPortResolver::ScanUsbCameras();
    const auto merge_session = [&cameras](const CameraSession* session) {
        if (session == nullptr || session->adapter == nullptr ||
            !session->adapter->IsConnected()) {
            return;
        }
        const CameraInfo info = session->adapter->CurrentCameraInfo();
        if (info.serial_number.empty()) return;
        const auto existing = std::find_if(
            cameras.begin(), cameras.end(), [&info](const CameraInfo& camera) {
                return camera.serial_number == info.serial_number;
            });
        if (existing == cameras.end()) {
            cameras.push_back(info);
        } else {
            *existing = info;
        }
    };

    merge_session(default_session_.get());
    for (const auto& entry : named_sessions_) {
        merge_session(entry.second.get());
    }
    return cameras;
}

bool CameraManager::ResolveSlotName(const std::string& slot_name,
                                    std::string* resolved_serial) const {
    if (resolved_serial == nullptr) {
        return false;
    }
    const auto it = slot_to_serial_.find(slot_name);
    if (it == slot_to_serial_.end()) {
        return false;  // 槽位未在配置文件中定义
    }
    if (it->second.empty()) {
        return false;  // 槽位已定义但对应端口无相机接入
    }
    *resolved_serial = it->second;
    return true;
}

}  // namespace youyeetoo
