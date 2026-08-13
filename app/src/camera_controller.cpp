#include "camera_controller.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <system_error>

#include "camera_sdk_adapter.h"
#include "user_data_store.h"

namespace youyeetoo {
namespace {

namespace fs = std::filesystem;
using SteadyClock = std::chrono::steady_clock;
constexpr std::size_t kMediaPreviewCount = 3;
constexpr int kDownloadTimeoutMsMin = 60000;
constexpr int kVideoDownloadTimeoutMsMin = 180000;
constexpr int kReconnectAttemptLimit = 10;
constexpr auto kFatalReconnectCooldown = std::chrono::seconds(30);

std::string NowTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#if defined(_WIN32)
    localtime_s(&local_tm, &now_time);
#else
    local_tm = *std::localtime(&now_time);
#endif

    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%F %T");
    return oss.str();
}

bool EndsWithIgnoreCase(const std::string& value, const std::string& suffix) {
    if (value.size() < suffix.size()) {
        return false;
    }
    const std::size_t offset = value.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        const char lhs = static_cast<char>(std::tolower(static_cast<unsigned char>(value[offset + i])));
        const char rhs = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
        if (lhs != rhs) {
            return false;
        }
    }
    return true;
}

bool IsVideoLikeMediaPath(const std::string& remote_path) {
    return EndsWithIgnoreCase(remote_path, ".insv") ||
           EndsWithIgnoreCase(remote_path, ".mp4") ||
           EndsWithIgnoreCase(remote_path, ".lrv");
}

std::optional<std::string> ExtractMediaTimestampToken(const std::string& remote_path) {
    const std::string file_name = fs::path(remote_path).filename().string();
    const std::size_t first_underscore = file_name.find('_');
    if (first_underscore == std::string::npos) {
        return std::nullopt;
    }
    if (first_underscore + 16 > file_name.size()) {
        return std::nullopt;
    }

    const std::string date_part = file_name.substr(first_underscore + 1, 8);
    const std::string separator = file_name.substr(first_underscore + 9, 1);
    const std::string time_part = file_name.substr(first_underscore + 10, 6);
    if (separator != "_") {
        return std::nullopt;
    }

    for (char ch : date_part) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return std::nullopt;
        }
    }
    for (char ch : time_part) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return std::nullopt;
        }
    }

    return date_part + time_part;
}

int DownloadTimeoutMsForRemotePath(const CameraAppOptions& options, const std::string& remote_path) {
    const int base_timeout_ms = std::max(options.timeout_ms, kDownloadTimeoutMsMin);
    if (IsVideoLikeMediaPath(remote_path)) {
        return std::max(base_timeout_ms * 3, kVideoDownloadTimeoutMsMin);
    }
    return base_timeout_ms;
}

std::string FormatElapsedMs(SteadyClock::duration duration) {
    return std::to_string(
               std::chrono::duration_cast<std::chrono::milliseconds>(duration).count()) +
           "ms";
}

}  // namespace

CameraController::CameraController(CameraSdkAdapter* adapter, UserDataStore* user_data_store)
    : adapter_(adapter), user_data_store_(user_data_store) {}

void CameraController::Configure(const CameraAppOptions& options) {
    options_ = options;
    next_poll_time_ = SteadyClock::now();
    next_reconnect_time_ = SteadyClock::now();
}

bool CameraController::StartTask(const CameraTaskRequest& request) {
    if (has_task_ || request.type == CameraTaskType::None) {
        return false;
    }

    task_ = TaskContext{};
    task_.request = request;
    if (task_.request.task_id == 0) {
        task_.request.task_id = NextTaskId();
    }
    if (task_.request.repeat_count <= 0) {
        task_.request.repeat_count = 1;
    }
    if (task_.request.output_dir.empty() && user_data_store_ != nullptr) {
        task_.request.output_dir = user_data_store_->DefaultOutputDir(options_);
    }
    task_.state_since = SteadyClock::now();
    task_.deadline = task_.state_since + std::chrono::milliseconds(options_.timeout_ms);

    has_task_ = true;
    snapshot_.task_type = task_.request.type;
    snapshot_.task_state = CameraTaskState::Queued;
    snapshot_.completed_iterations = 0;
    snapshot_.total_iterations = task_.request.repeat_count;
    snapshot_.outputs.clear();
    snapshot_.result_summary.clear();
    snapshot_.error_message.clear();
    return true;
}

void CameraController::CancelTask(const std::string& reason) {
    if (!has_task_) {
        return;
    }
    snapshot_.error_message = reason;
    snapshot_.task_state = CameraTaskState::Cancelled;
    ResetTask();
}

void CameraController::Tick() {
    const auto now = SteadyClock::now();
    TickConnection(now);
    TickStatusPoll(now);
    TickTask(now);
}

const CameraControllerSnapshot& CameraController::snapshot() const {
    return snapshot_;
}

bool CameraController::HasActiveTask() const {
    return has_task_;
}

bool CameraController::IsTaskFinished() const {
    return snapshot_.task_state == CameraTaskState::Completed ||
           snapshot_.task_state == CameraTaskState::Failed ||
           snapshot_.task_state == CameraTaskState::Timeout ||
           snapshot_.task_state == CameraTaskState::Cancelled;
}

void CameraController::SetDeviceListSupplements(const std::vector<CameraInfo>& cameras) {
    device_list_supplements_ = cameras;
}

std::string CameraController::DeviceStateToString(CameraDeviceState state) {
    switch (state) {
    case CameraDeviceState::Unavailable:
        return "UNAVAILABLE";
    case CameraDeviceState::Connecting:
        return "CONNECTING";
    case CameraDeviceState::Idle:
        return "IDLE";
    case CameraDeviceState::Busy:
        return "BUSY";
    case CameraDeviceState::ErrorRecover:
        return "ERROR_RECOVER";
    case CameraDeviceState::Fatal:
        return "FATAL";
    }
    return "UNAVAILABLE";
}

std::string CameraController::TaskTypeToString(CameraTaskType type) {
    switch (type) {
    case CameraTaskType::None:
        return "NONE";
    case CameraTaskType::AutoPhotoSequence:
        return "AUTO_PHOTO_SEQUENCE";
    case CameraTaskType::TakePhotoDownload:
        return "TAKE_PHOTO_DOWNLOAD";
    case CameraTaskType::SetMode:
        return "SET_MODE";
    case CameraTaskType::SetParam:
        return "SET_PARAM";
    case CameraTaskType::GetParam:
        return "GET_PARAM";
    case CameraTaskType::GetMediaTime:
        return "GET_MEDIA_TIME";
    case CameraTaskType::GetLog:
        return "GET_LOG";
    case CameraTaskType::ListDevices:
        return "LIST_DEVICES";
    case CameraTaskType::PreviewStart:
        return "PREVIEW_START";
    case CameraTaskType::PreviewStop:
        return "PREVIEW_STOP";
    case CameraTaskType::ShutdownMaint:
        return "SHUTDOWN_MAINT";
    case CameraTaskType::StartRecording:
        return "START_RECORDING";
    case CameraTaskType::StopRecording:
        return "STOP_RECORDING";
    case CameraTaskType::StartExtendedCapture:
        return "START_EXTENDED_CAPTURE";
    case CameraTaskType::StopExtendedCapture:
        return "STOP_EXTENDED_CAPTURE";
    case CameraTaskType::ListMedia:
        return "LIST_MEDIA";
    case CameraTaskType::DownloadMedia:
        return "DOWNLOAD_MEDIA";
    case CameraTaskType::BatchDownload:
        return "BATCH_DOWNLOAD";
    case CameraTaskType::DeleteMedia:
        return "DELETE_MEDIA";
    case CameraTaskType::DeleteAllMedia:
        return "DELETE_ALL_MEDIA";
    case CameraTaskType::Initialize:
        return "INITIALIZE";
    case CameraTaskType::GetStatus:
        return "GET_STATUS";
    case CameraTaskType::GetCaptureStatus:
        return "GET_CAPTURE_STATUS";
    case CameraTaskType::GetBattery:
        return "GET_BATTERY";
    case CameraTaskType::GetStorage:
        return "GET_STORAGE";
    }
    return "NONE";
}

std::string CameraController::TaskStateToString(CameraTaskState state) {
    switch (state) {
    case CameraTaskState::None:
        return "NONE";
    case CameraTaskState::Queued:
        return "QUEUED";
    case CameraTaskState::Precheck:
        return "PRECHECK";
    case CameraTaskState::Capturing:
        return "CAPTURING";
    case CameraTaskState::Configuring:
        return "CONFIGURING";
    case CameraTaskState::Recording:
        return "RECORDING";
    case CameraTaskState::StoppingRecord:
        return "STOPPING_RECORD";
    case CameraTaskState::Listing:
        return "LISTING";
    case CameraTaskState::Downloading:
        return "DOWNLOADING";
    case CameraTaskState::Deleting:
        return "DELETING";
    case CameraTaskState::Verifying:
        return "VERIFYING";
    case CameraTaskState::Completed:
        return "COMPLETED";
    case CameraTaskState::Failed:
        return "FAILED";
    case CameraTaskState::Timeout:
        return "TIMEOUT";
    case CameraTaskState::Cancelled:
        return "CANCELLED";
    }
    return "NONE";
}

void CameraController::ResetTask() {
    has_task_ = false;
    task_ = TaskContext{};
    snapshot_.task_type = CameraTaskType::None;
    if (snapshot_.device_state == CameraDeviceState::Busy) {
        SetDeviceState(CameraDeviceState::Idle);
    }
}

void CameraController::SetDeviceState(CameraDeviceState state) {
    snapshot_.device_state = state;
}

void CameraController::SetTaskState(CameraTaskState state) {
    snapshot_.task_state = state;
    task_.state_since = SteadyClock::now();
    task_.deadline = task_.state_since + std::chrono::milliseconds(options_.timeout_ms);
}

void CameraController::FailTask(CameraTaskState state, const std::string& error_message) {
    snapshot_.error_message = error_message;
    snapshot_.result_summary.clear();
    snapshot_.task_state = state;
    if (adapter_ != nullptr && !adapter_->IsConnected()) {
        SetDeviceState(CameraDeviceState::ErrorRecover);
        next_reconnect_time_ = SteadyClock::now() + std::chrono::milliseconds(options_.poll_interval_ms);
    } else {
        SetDeviceState(CameraDeviceState::Idle);
    }
    ResetTask();
}

void CameraController::CompleteTask(const std::string& summary) {
    snapshot_.result_summary = summary;
    snapshot_.error_message.clear();
    snapshot_.task_state = CameraTaskState::Completed;
    SetDeviceState(CameraDeviceState::Idle);
    ResetTask();
}

void CameraController::TickConnection(std::chrono::steady_clock::time_point now) {
    if (adapter_ == nullptr) {
        SetDeviceState(CameraDeviceState::Fatal);
        snapshot_.error_message = "camera adapter is null";
        return;
    }

    if (adapter_->IsConnected()) {
        if (snapshot_.device_state == CameraDeviceState::Connecting ||
            snapshot_.device_state == CameraDeviceState::Unavailable ||
            snapshot_.device_state == CameraDeviceState::ErrorRecover) {
            SetDeviceState(has_task_ ? CameraDeviceState::Busy : CameraDeviceState::Idle);
        }
        return;
    }

    if (snapshot_.recover_attempts >= kReconnectAttemptLimit) {
        if (snapshot_.device_state != CameraDeviceState::Fatal) {
            SetDeviceState(CameraDeviceState::Fatal);
            snapshot_.error_message = "reconnect attempts exhausted; retrying after cooldown";
            next_reconnect_time_ = now + kFatalReconnectCooldown;
        }
        if (now < next_reconnect_time_) {
            return;
        }
        snapshot_.recover_attempts = 0;
        snapshot_.error_message.clear();
        SetDeviceState(CameraDeviceState::ErrorRecover);
    }

    if (now < next_reconnect_time_) {
        if (snapshot_.device_state != CameraDeviceState::ErrorRecover) {
            SetDeviceState(CameraDeviceState::Unavailable);
        }
        return;
    }

    SetDeviceState(CameraDeviceState::Connecting);
    const bool opened = adapter_->OpenCamera(options_.serial_number, options_.timeout_ms);
    std::cout << "[CameraController] reconnect attempt result opened="
              << (opened ? "true" : "false")
              << " serial="
              << (options_.serial_number.empty() ? "(any)" : options_.serial_number)
              << " connected_now=" << (adapter_->IsConnected() ? "true" : "false")
              << " recover_attempts=" << snapshot_.recover_attempts
              << std::endl;
    if (opened) {
        snapshot_.recover_attempts = 0;
        SetDeviceState(has_task_ ? CameraDeviceState::Busy : CameraDeviceState::Idle);
        return;
    }

    ++snapshot_.recover_attempts;
    SetDeviceState(CameraDeviceState::ErrorRecover);
    snapshot_.runtime_status.connected = false;
    snapshot_.runtime_status.recent_error = "camera_open_failed";
    next_reconnect_time_ = now + std::chrono::milliseconds(options_.poll_interval_ms);
}

void CameraController::TickStatusPoll(std::chrono::steady_clock::time_point now) {
    if (now < next_poll_time_) {
        return;
    }
    snapshot_.runtime_status = CollectRuntimeStatus();
    next_poll_time_ = now + std::chrono::milliseconds(options_.poll_interval_ms);
}

void CameraController::TickTask(std::chrono::steady_clock::time_point now) {
    if (!has_task_) {
        return;
    }

    if (task_.deadline < now) {
        FailTask(CameraTaskState::Timeout, "task deadline exceeded");
        return;
    }

    switch (task_.request.type) {
    case CameraTaskType::AutoPhotoSequence:
    case CameraTaskType::TakePhotoDownload:
        if (snapshot_.task_state == CameraTaskState::Queued ||
            snapshot_.task_state == CameraTaskState::Precheck) {
            BeginPhotoTask(now);
        } else if (snapshot_.task_state == CameraTaskState::Capturing ||
                   snapshot_.task_state == CameraTaskState::Downloading ||
                   snapshot_.task_state == CameraTaskState::Verifying) {
            ContinuePhotoTask(now);
        }
        return;

    case CameraTaskType::SetMode:
        BeginSetModeTask();
        return;

    case CameraTaskType::SetParam:
        BeginSetParamTask();
        return;

    case CameraTaskType::GetParam:
        BeginGetParamTask();
        return;

    case CameraTaskType::GetMediaTime:
        BeginGetMediaTimeTask();
        return;

    case CameraTaskType::GetLog:
        BeginGetLogTask();
        return;

    case CameraTaskType::ListDevices:
        BeginListDevicesTask();
        return;

    case CameraTaskType::PreviewStart:
        BeginPreviewStartTask();
        return;

    case CameraTaskType::PreviewStop:
        BeginPreviewStopTask();
        return;

    case CameraTaskType::ShutdownMaint:
        BeginShutdownMaintTask();
        return;

    case CameraTaskType::StartRecording:
        BeginRecordStartTask();
        return;

    case CameraTaskType::StopRecording:
        BeginRecordStopTask();
        return;

    case CameraTaskType::StartExtendedCapture:
        BeginExtendedCaptureStartTask();
        return;

    case CameraTaskType::StopExtendedCapture:
        BeginExtendedCaptureStopTask();
        return;

    case CameraTaskType::ListMedia:
        BeginListMediaTask();
        return;

    case CameraTaskType::DownloadMedia:
        BeginDownloadMediaTask();
        return;

    case CameraTaskType::BatchDownload:
        BeginBatchDownloadTask();
        return;

    case CameraTaskType::DeleteMedia:
        BeginDeleteMediaTask();
        return;

    case CameraTaskType::DeleteAllMedia:
        BeginDeleteAllMediaTask();
        return;

    case CameraTaskType::Initialize:
        BeginInitializeTask();
        return;

    case CameraTaskType::GetStatus:
        BeginGetStatusTask();
        return;

    case CameraTaskType::GetCaptureStatus:
        BeginGetStatusTask();
        return;

    case CameraTaskType::GetBattery:
        BeginGetBatteryTask();
        return;

    case CameraTaskType::GetStorage:
        BeginGetStorageTask();
        return;

    case CameraTaskType::None:
        return;
    }
}

bool CameraController::BeginSetModeTask() {
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        FailTask(CameraTaskState::Failed, "camera is not connected");
        return false;
    }

    const auto mode_it = task_.request.parameters.find("mode");
    if (mode_it == task_.request.parameters.end() || mode_it->second.empty()) {
        FailTask(CameraTaskState::Failed, "set mode requires mode parameter");
        return false;
    }

    SetDeviceState(CameraDeviceState::Busy);
    SetTaskState(CameraTaskState::Configuring);

    std::string error_message;
    if (!adapter_->SetCaptureMode(mode_it->second, &error_message)) {
        FailTask(CameraTaskState::Failed, error_message);
        return false;
    }

    snapshot_.completed_iterations = 1;
    CompleteTask("camera mode set mode=" + mode_it->second);
    return true;
}

bool CameraController::BeginSetParamTask() {
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        FailTask(CameraTaskState::Failed, "camera is not connected");
        return false;
    }

    SetDeviceState(CameraDeviceState::Busy);
    SetTaskState(CameraTaskState::Configuring);

    std::string summary;
    std::string error_message;
    if (!adapter_->SetCaptureParameters(task_.request.parameters, &summary, &error_message)) {
        FailTask(CameraTaskState::Failed, error_message);
        return false;
    }

    snapshot_.completed_iterations = 1;
    CompleteTask(summary.empty() ? "camera parameters applied" : summary);
    return true;
}

bool CameraController::BeginGetParamTask() {
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        FailTask(CameraTaskState::Failed, "camera is not connected");
        return false;
    }

    SetDeviceState(CameraDeviceState::Busy);
    SetTaskState(CameraTaskState::Configuring);

    std::string json_body;
    std::string summary;
    std::string error_message;
    if (!adapter_->GetCaptureParameters(task_.request.parameters,
                                        &json_body,
                                        &summary,
                                        &error_message)) {
        FailTask(CameraTaskState::Failed, error_message);
        return false;
    }

    std::string output_path;
    if (!WriteParamQueryResultFile(json_body, &output_path, &error_message)) {
        FailTask(CameraTaskState::Failed, error_message);
        return false;
    }

    snapshot_.completed_iterations = 1;
    snapshot_.outputs.clear();
    snapshot_.outputs.push_back(output_path);
    CompleteTask((summary.empty() ? "camera parameters queried" : summary) + " output=" +
                 output_path);
    return true;
}

bool CameraController::BeginGetMediaTimeTask() {
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        FailTask(CameraTaskState::Failed, "camera is not connected");
        return false;
    }

    SetDeviceState(CameraDeviceState::Busy);
    SetTaskState(CameraTaskState::Configuring);

    const int64_t media_time = adapter_->GetCameraMediaTime();
    if (media_time < 0) {
        FailTask(CameraTaskState::Failed, "camera media time unavailable");
        return false;
    }

    std::string output_path;
    std::string error_message;
    if (!WriteMediaTimeResultFile(media_time, &output_path, &error_message)) {
        FailTask(CameraTaskState::Failed, error_message);
        return false;
    }

    snapshot_.runtime_status.media_time = media_time;
    snapshot_.runtime_status.media_time_ok = true;
    snapshot_.completed_iterations = 1;
    snapshot_.outputs.clear();
    snapshot_.outputs.push_back(output_path);
    CompleteTask("camera media time=" + std::to_string(media_time) + " output=" + output_path);
    return true;
}

bool CameraController::BeginGetLogTask() {
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        FailTask(CameraTaskState::Failed, "camera is not connected");
        return false;
    }

    SetDeviceState(CameraDeviceState::Busy);
    SetTaskState(CameraTaskState::Configuring);

    const std::string log_url = adapter_->GetCameraLogFileUrl();
    if (log_url.empty()) {
        FailTask(CameraTaskState::Failed, "camera log url unavailable");
        return false;
    }

    std::string output_path;
    std::string error_message;
    if (!WriteLogQueryResultFile(log_url, &output_path, &error_message)) {
        FailTask(CameraTaskState::Failed, error_message);
        return false;
    }

    snapshot_.completed_iterations = 1;
    snapshot_.outputs.clear();
    snapshot_.outputs.push_back(output_path);
    CompleteTask("camera log url=" + log_url + " output=" + output_path);
    return true;
}

bool CameraController::BeginListDevicesTask() {
    if (adapter_ == nullptr) {
        FailTask(CameraTaskState::Failed, "camera adapter is null");
        return false;
    }

    SetDeviceState(CameraDeviceState::Busy);
    SetTaskState(CameraTaskState::Listing);

    std::vector<CameraInfo> cameras = adapter_->ListCameras();
    for (const auto& supplement : device_list_supplements_) {
        if (supplement.serial_number.empty()) continue;
        const auto existing = std::find_if(
            cameras.begin(), cameras.end(), [&supplement](const CameraInfo& camera) {
                return camera.serial_number == supplement.serial_number;
            });
        if (existing == cameras.end()) {
            cameras.push_back(supplement);
            continue;
        }
        if ((existing->camera_name.empty() || existing->camera_name == "(active-session)") &&
            !supplement.camera_name.empty()) {
            existing->camera_name = supplement.camera_name;
        }
        if ((existing->firmware_version.empty() ||
             existing->firmware_version == "(active-session)") &&
            !supplement.firmware_version.empty()) {
            existing->firmware_version = supplement.firmware_version;
        }
        if ((existing->model_key.empty() || existing->model_key == "Unknown" ||
             existing->model_key == "unknown") && !supplement.model_key.empty()) {
            existing->model_key = supplement.model_key;
        }
        if (existing->capabilities.empty() && !supplement.capabilities.empty()) {
            existing->capabilities = supplement.capabilities;
        }
    }
    std::string output_path;
    std::string error_message;
    if (!WriteDeviceListResultFile(cameras, &output_path, &error_message)) {
        FailTask(CameraTaskState::Failed, error_message);
        return false;
    }

    snapshot_.completed_iterations = static_cast<int>(cameras.size());
    snapshot_.total_iterations = static_cast<int>(cameras.size());
    snapshot_.outputs.clear();
    snapshot_.outputs.push_back(output_path);
    CompleteTask("listed camera devices count=" + std::to_string(cameras.size()) +
                 " output=" + output_path);
    return true;
}

bool CameraController::BeginInitializeTask() {
    if (adapter_ == nullptr) {
        FailTask(CameraTaskState::Failed, "camera adapter is null");
        return false;
    }

    SetDeviceState(CameraDeviceState::Connecting);
    SetTaskState(CameraTaskState::Precheck);
    if (adapter_->IsConnected()) {
        snapshot_.recover_attempts = 0;
        snapshot_.runtime_status = CollectRuntimeStatus();
        snapshot_.completed_iterations = 1;
        CompleteTask("camera already initialized serial=" + ActiveSerialNumber());
        return true;
    }

    snapshot_.recover_attempts = 0;
    next_reconnect_time_ = SteadyClock::now();
    if (!adapter_->OpenCamera(options_.serial_number, options_.timeout_ms)) {
        snapshot_.recover_attempts = 1;
        next_reconnect_time_ = SteadyClock::now() +
                               std::chrono::milliseconds(options_.poll_interval_ms);
        FailTask(CameraTaskState::Failed, "camera initialization failed; automatic retry scheduled");
        return false;
    }

    snapshot_.runtime_status = CollectRuntimeStatus();
    snapshot_.completed_iterations = 1;
    CompleteTask("camera initialized serial=" + ActiveSerialNumber());
    return true;
}

bool CameraController::BeginGetStatusTask() {
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        FailTask(CameraTaskState::Failed, "camera is not connected");
        return false;
    }

    SetDeviceState(CameraDeviceState::Busy);
    SetTaskState(CameraTaskState::Configuring);
    snapshot_.runtime_status.connected = true;
    snapshot_.runtime_status.capturing = adapter_->CaptureCurrentStatus();
    snapshot_.runtime_status.recent_error.clear();
    snapshot_.completed_iterations = 1;
    CompleteTask(std::string("camera status connected=yes capturing=") +
                 (snapshot_.runtime_status.capturing ? "yes" : "no"));
    return true;
}

bool CameraController::BeginGetBatteryTask() {
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        FailTask(CameraTaskState::Failed, "camera is not connected");
        return false;
    }

    SetDeviceState(CameraDeviceState::Busy);
    SetTaskState(CameraTaskState::Configuring);
    BatteryInfo battery;
    if (!adapter_->GetBatteryStatus(&battery)) {
        snapshot_.runtime_status.battery_ok = false;
        FailTask(CameraTaskState::Failed, "failed to query camera battery");
        return false;
    }
    snapshot_.runtime_status.connected = true;
    snapshot_.runtime_status.battery = battery;
    snapshot_.runtime_status.battery_ok = true;
    snapshot_.runtime_status.recent_error.clear();
    snapshot_.completed_iterations = 1;
    CompleteTask("camera battery=" + std::to_string(battery.battery_level) + "/" +
                 std::to_string(battery.battery_scale) + " power_type=" + battery.power_type);
    return true;
}

bool CameraController::BeginGetStorageTask() {
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        FailTask(CameraTaskState::Failed, "camera is not connected");
        return false;
    }

    SetDeviceState(CameraDeviceState::Busy);
    SetTaskState(CameraTaskState::Configuring);
    StorageInfo storage;
    if (!adapter_->GetStorageState(&storage)) {
        snapshot_.runtime_status.storage_ok = false;
        FailTask(CameraTaskState::Failed, "failed to query camera storage");
        return false;
    }
    snapshot_.runtime_status.connected = true;
    snapshot_.runtime_status.storage = storage;
    snapshot_.runtime_status.storage_ok = true;
    snapshot_.runtime_status.recent_error.clear();
    snapshot_.completed_iterations = 1;
    CompleteTask("camera storage state=" + storage.state +
                 " free=" + std::to_string(storage.free_space) +
                 " total=" + std::to_string(storage.total_space));
    return true;
}

bool CameraController::BeginPreviewStartTask() {
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        FailTask(CameraTaskState::Failed, "camera is not connected");
        return false;
    }

    SetDeviceState(CameraDeviceState::Busy);
    SetTaskState(CameraTaskState::Configuring);

    std::string summary;
    std::string error_message;
    if (!adapter_->StartPreview(task_.request.parameters, &summary, &error_message)) {
        FailTask(CameraTaskState::Failed, error_message);
        return false;
    }

    snapshot_.completed_iterations = 1;
    CompleteTask(summary.empty() ? "camera preview started" : summary);
    return true;
}

bool CameraController::BeginPreviewStopTask() {
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        FailTask(CameraTaskState::Failed, "camera is not connected");
        return false;
    }

    SetDeviceState(CameraDeviceState::Busy);
    SetTaskState(CameraTaskState::Configuring);

    std::string summary;
    std::string error_message;
    if (!adapter_->StopPreview(&summary, &error_message)) {
        FailTask(CameraTaskState::Failed, error_message);
        return false;
    }

    snapshot_.completed_iterations = 1;
    CompleteTask(summary.empty() ? "camera preview stopped" : summary);
    return true;
}

bool CameraController::BeginShutdownMaintTask() {
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        FailTask(CameraTaskState::Failed, "camera is not connected");
        return false;
    }

    SetDeviceState(CameraDeviceState::Busy);
    SetTaskState(CameraTaskState::Configuring);

    std::string error_message;
    if (!adapter_->ShutdownForMaintenance(&error_message)) {
        FailTask(CameraTaskState::Failed, error_message);
        return false;
    }

    snapshot_.completed_iterations = 1;
    CompleteTask("camera maintenance shutdown requested");
    return true;
}

bool CameraController::BeginPhotoTask(std::chrono::steady_clock::time_point now) {
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        FailTask(CameraTaskState::Failed, "camera is not connected");
        return false;
    }

    std::string error_message;
    if (user_data_store_ == nullptr ||
        !user_data_store_->EnsureDirectoryExists(task_.request.output_dir, &error_message)) {
        FailTask(CameraTaskState::Failed, error_message);
        return false;
    }

    SetDeviceState(CameraDeviceState::Busy);
    SetTaskState(CameraTaskState::Precheck);
    task_.deadline = now + std::chrono::milliseconds(options_.timeout_ms);

    if (!adapter_->SetNormalPhotoMode()) {
        FailTask(CameraTaskState::Failed, "failed to switch to normal photo mode");
        return false;
    }

    SetTaskState(CameraTaskState::Capturing);
    const auto capture = adapter_->TakePhoto();
    if (capture.origin_urls.empty()) {
        FailTask(CameraTaskState::Failed, "take photo returned no media url");
        return false;
    }

    task_.pending_remote_urls = capture.origin_urls;
    task_.download_index = 0;
    SetTaskState(CameraTaskState::Downloading);
    return ContinuePhotoTask(now);
}

bool CameraController::ContinuePhotoTask(std::chrono::steady_clock::time_point now) {
    if (snapshot_.task_state == CameraTaskState::Downloading) {
        if (!DownloadCurrentRemoteFile()) {
            return false;
        }

        ++task_.download_index;
        if (task_.download_index < task_.pending_remote_urls.size()) {
            SetTaskState(CameraTaskState::Downloading);
            task_.deadline = now + std::chrono::milliseconds(options_.timeout_ms);
            return true;
        }

        SetTaskState(CameraTaskState::Verifying);
    }

    if (snapshot_.task_state == CameraTaskState::Verifying) {
        ++snapshot_.completed_iterations;
        if (snapshot_.completed_iterations < snapshot_.total_iterations &&
            task_.request.type == CameraTaskType::AutoPhotoSequence) {
            task_.pending_remote_urls.clear();
            task_.download_index = 0;
            SetTaskState(CameraTaskState::Capturing);
            return BeginPhotoTask(now);
        }

        std::ostringstream oss;
        oss << "downloaded " << snapshot_.outputs.size() << " file(s)";
        CompleteTask(oss.str());
        return true;
    }

    return true;
}

bool CameraController::BeginRecordStartTask() {
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        FailTask(CameraTaskState::Failed, "camera is not connected");
        return false;
    }

    SetDeviceState(CameraDeviceState::Busy);
    SetTaskState(CameraTaskState::Recording);
    std::string summary;
    std::string error_message;
    if (!adapter_->StartRecording(task_.request.parameters, &summary, &error_message)) {
        FailTask(CameraTaskState::Failed,
                 error_message.empty() ? "failed to start recording" : error_message);
        return false;
    }

    snapshot_.completed_iterations = 1;
    CompleteTask(summary.empty() ? "recording started" : summary);
    return true;
}

bool CameraController::BeginRecordStopTask() {
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        FailTask(CameraTaskState::Failed, "camera is not connected");
        return false;
    }

    SetDeviceState(CameraDeviceState::Busy);
    SetTaskState(CameraTaskState::StoppingRecord);
    const auto result = adapter_->StopRecording();
    if (result.origin_urls.empty() && result.low_resolution_urls.empty()) {
        FailTask(CameraTaskState::Failed, "failed to stop recording or no media returned");
        return false;
    }

    snapshot_.outputs = result.origin_urls;
    snapshot_.outputs.insert(snapshot_.outputs.end(),
                             result.low_resolution_urls.begin(),
                             result.low_resolution_urls.end());
    snapshot_.completed_iterations = 1;

    std::ostringstream oss;
    oss << "recording stopped origin_urls=" << result.origin_urls.size()
        << " lrv_urls=" << result.low_resolution_urls.size();
    CompleteTask(oss.str());
    return true;
}

bool CameraController::BeginExtendedCaptureStartTask() {
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        FailTask(CameraTaskState::Failed, "camera is not connected");
        return false;
    }

    if (task_.request.extended_mode.empty()) {
        FailTask(CameraTaskState::Failed, "extended capture start requires mode");
        return false;
    }

    SetDeviceState(CameraDeviceState::Busy);
    if (task_.request.extended_mode == "interval_photo") {
        SetTaskState(CameraTaskState::Capturing);
    } else {
        SetTaskState(CameraTaskState::Recording);
    }

    std::string error_message;
    if (!adapter_->StartExtendedCapture(task_.request.extended_mode, &error_message)) {
        FailTask(CameraTaskState::Failed, error_message);
        return false;
    }

    snapshot_.completed_iterations = 1;
    CompleteTask("extended capture started mode=" + task_.request.extended_mode);
    return true;
}

bool CameraController::BeginExtendedCaptureStopTask() {
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        FailTask(CameraTaskState::Failed, "camera is not connected");
        return false;
    }

    if (task_.request.extended_mode.empty()) {
        FailTask(CameraTaskState::Failed, "extended capture stop requires mode");
        return false;
    }

    SetDeviceState(CameraDeviceState::Busy);
    if (task_.request.extended_mode == "interval_photo") {
        SetTaskState(CameraTaskState::Capturing);
    } else {
        SetTaskState(CameraTaskState::StoppingRecord);
    }

    std::string error_message;
    const auto result = adapter_->StopExtendedCapture(task_.request.extended_mode, &error_message);
    if (result.origin_urls.empty() && result.low_resolution_urls.empty()) {
        FailTask(CameraTaskState::Failed,
                 error_message.empty() ? "extended capture stop returned no media" : error_message);
        return false;
    }

    snapshot_.outputs = result.origin_urls;
    snapshot_.outputs.insert(snapshot_.outputs.end(),
                             result.low_resolution_urls.begin(),
                             result.low_resolution_urls.end());
    snapshot_.completed_iterations = 1;

    std::ostringstream oss;
    oss << "extended capture stopped mode=" << task_.request.extended_mode
        << " origin_urls=" << result.origin_urls.size()
        << " lrv_urls=" << result.low_resolution_urls.size();
    CompleteTask(oss.str());
    return true;
}

bool CameraController::BeginListMediaTask() {
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        FailTask(CameraTaskState::Failed, "camera is not connected");
        return false;
    }

    SetDeviceState(CameraDeviceState::Busy);
    SetTaskState(CameraTaskState::Listing);

    int count = 0;
    const bool count_ok = adapter_->GetMediaCount(&count);
    const std::vector<std::string> files = adapter_->ListMedia();
    if (!count_ok && files.empty()) {
        FailTask(CameraTaskState::Failed, "failed to list camera media");
        return false;
    }

    snapshot_.outputs = files;
    snapshot_.completed_iterations = 1;
    std::string media_list_path;
    std::string media_list_json_path;
    std::string write_error;
    if (!WriteLatestMediaListFile(files, count_ok ? count : static_cast<int>(files.size()),
                                  &media_list_path, &write_error)) {
        FailTask(CameraTaskState::Failed, write_error);
        return false;
    }
    if (!WriteLatestMediaListJsonFile(files, count_ok ? count : static_cast<int>(files.size()),
                                      &media_list_json_path, &write_error)) {
        FailTask(CameraTaskState::Failed, write_error);
        return false;
    }

    std::ostringstream oss;
    oss << "listed " << files.size() << " file(s)";
    if (count_ok) {
        oss << " reported_count=" << count;
    }
    if (!files.empty()) {
        oss << " preview=";
        for (std::size_t i = 0; i < files.size() && i < kMediaPreviewCount; ++i) {
            if (i != 0) {
                oss << ",";
            }
            oss << files[i];
        }
    }
    oss << " media_list_file=" << media_list_path;
    oss << " media_list_json=" << media_list_json_path;
    CompleteTask(oss.str());
    return true;
}

bool CameraController::BeginDownloadMediaTask() {
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        FailTask(CameraTaskState::Failed, "camera is not connected");
        return false;
    }

    if (task_.request.remote_path.empty()) {
        FailTask(CameraTaskState::Failed, "download task requires remote_path");
        return false;
    }

    std::string error_message;
    const std::string output_dir =
        task_.request.output_dir.empty() && user_data_store_ != nullptr
            ? user_data_store_->DefaultOutputDir(options_)
            : task_.request.output_dir;
    if (user_data_store_ == nullptr ||
        !user_data_store_->EnsureDirectoryExists(output_dir, &error_message)) {
        FailTask(CameraTaskState::Failed, error_message);
        return false;
    }

    std::string local_path = task_.request.local_path;
    if (local_path.empty()) {
        local_path = JoinPath(output_dir, FileNameFromUrl(task_.request.remote_path));
    }

    SetDeviceState(CameraDeviceState::Busy);
    SetTaskState(CameraTaskState::Downloading);
    if (!FinalizeSingleDownload(task_.request.remote_path, local_path)) {
        return false;
    }

    snapshot_.completed_iterations = 1;
    std::ostringstream oss;
    oss << "downloaded 1 file to " << local_path;
    CompleteTask(oss.str());
    return true;
}

bool CameraController::BeginBatchDownloadTask() {
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        FailTask(CameraTaskState::Failed, "camera is not connected");
        return false;
    }

    std::string output_dir =
        task_.request.output_dir.empty() && user_data_store_ != nullptr
            ? JoinPath(user_data_store_->DefaultOutputDir(options_), "batch")
            : task_.request.output_dir;
    std::string error_message;
    if (user_data_store_ == nullptr ||
        !user_data_store_->EnsureDirectoryExists(output_dir, &error_message)) {
        FailTask(CameraTaskState::Failed, error_message);
        return false;
    }

    std::vector<std::string> remote_files;
    if (!LoadLatestMediaList(&remote_files, &error_message)) {
        FailTask(CameraTaskState::Failed, error_message);
        return false;
    }

    if (remote_files.empty()) {
        FailTask(CameraTaskState::Failed, "latest media list contains no file");
        return false;
    }

    const std::string normalized_time_from = NormalizeTimeFilterToken(task_.request.batch_time_from);
    const std::string normalized_time_to = NormalizeTimeFilterToken(task_.request.batch_time_to);
    if ((!task_.request.batch_time_from.empty() && normalized_time_from.empty()) ||
        (!task_.request.batch_time_to.empty() && normalized_time_to.empty())) {
        FailTask(CameraTaskState::Failed,
                 "invalid batch time filter, expected YYYYMMDD_HHMMSS or YYYYMMDDHHMMSS");
        return false;
    }
    if (!normalized_time_from.empty() && !normalized_time_to.empty() &&
        normalized_time_from > normalized_time_to) {
        FailTask(CameraTaskState::Failed, "batch time filter time_from is later than time_to");
        return false;
    }

    std::vector<std::string> filtered_remote_files;
    filtered_remote_files.reserve(remote_files.size());
    for (const auto& remote_path : remote_files) {
        if (normalized_time_from.empty() && normalized_time_to.empty()) {
            filtered_remote_files.push_back(remote_path);
            continue;
        }

        const auto timestamp_token = ExtractMediaTimestampToken(remote_path);
        if (!timestamp_token.has_value()) {
            continue;
        }
        if (!normalized_time_from.empty() && *timestamp_token < normalized_time_from) {
            continue;
        }
        if (!normalized_time_to.empty() && *timestamp_token > normalized_time_to) {
            continue;
        }
        filtered_remote_files.push_back(remote_path);
    }

    if (filtered_remote_files.empty()) {
        std::ostringstream oss;
        oss << "latest media list contains no file within time filter";
        if (!normalized_time_from.empty()) {
            oss << " time_from=" << normalized_time_from;
        }
        if (!normalized_time_to.empty()) {
            oss << " time_to=" << normalized_time_to;
        }
        FailTask(CameraTaskState::Failed, oss.str());
        return false;
    }

    SetDeviceState(CameraDeviceState::Busy);
    SetTaskState(CameraTaskState::Downloading);
    snapshot_.outputs.clear();

    for (const auto& remote_path : filtered_remote_files) {
        const std::string local_path = JoinPath(output_dir, FileNameFromUrl(remote_path));
        if (!FinalizeSingleDownload(remote_path, local_path)) {
            return false;
        }
    }

    snapshot_.completed_iterations = static_cast<int>(snapshot_.outputs.size());
    snapshot_.total_iterations = static_cast<int>(filtered_remote_files.size());

    std::string summary_path;
    if (!WriteBatchDownloadSummaryFile(snapshot_.outputs, &summary_path, &error_message)) {
        FailTask(CameraTaskState::Failed, error_message);
        return false;
    }

    std::ostringstream oss;
    oss << "batch downloaded " << snapshot_.outputs.size() << " file(s)"
        << " summary_file=" << summary_path;
    if (!normalized_time_from.empty()) {
        oss << " time_from=" << normalized_time_from;
    }
    if (!normalized_time_to.empty()) {
        oss << " time_to=" << normalized_time_to;
    }
    oss << " matched=" << filtered_remote_files.size()
        << " listed_total=" << remote_files.size();
    CompleteTask(oss.str());
    return true;
}

bool CameraController::BeginDeleteMediaTask() {
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        FailTask(CameraTaskState::Failed, "camera is not connected");
        return false;
    }

    if (task_.request.remote_path.empty()) {
        FailTask(CameraTaskState::Failed, "delete task requires remote_path");
        return false;
    }
    if (!task_.request.delete_confirmed) {
        FailTask(CameraTaskState::Failed, "delete task requires explicit confirmation");
        return false;
    }

    SetDeviceState(CameraDeviceState::Busy);
    SetTaskState(CameraTaskState::Deleting);
    if (!adapter_->DeleteMedia(task_.request.remote_path)) {
        FailTask(CameraTaskState::Failed, "failed to delete media " + task_.request.remote_path);
        return false;
    }

    snapshot_.completed_iterations = 1;
    snapshot_.outputs.clear();
    snapshot_.outputs.push_back(task_.request.remote_path);
    CompleteTask("deleted media " + task_.request.remote_path);
    return true;
}

bool CameraController::BeginDeleteAllMediaTask() {
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        FailTask(CameraTaskState::Failed, "camera is not connected");
        return false;
    }
    if (!task_.request.delete_confirmed) {
        FailTask(CameraTaskState::Failed, "delete-all task requires explicit confirmation");
        return false;
    }

    const auto confirm_it = task_.request.parameters.find("confirm");
    if (confirm_it == task_.request.parameters.end() || confirm_it->second != "DELETE_ALL") {
        FailTask(CameraTaskState::Failed,
                 "delete-all task requires runtime parameter confirm=DELETE_ALL");
        return false;
    }

    SetDeviceState(CameraDeviceState::Busy);
    SetTaskState(CameraTaskState::Deleting);

    int count = 0;
    const bool count_ok = adapter_->GetMediaCount(&count);
    const std::vector<std::string> files = adapter_->ListMedia();
    if (!count_ok && files.empty()) {
        FailTask(CameraTaskState::Failed, "failed to list camera media before delete-all");
        return false;
    }

    if (files.empty()) {
        snapshot_.completed_iterations = 1;
        snapshot_.outputs.clear();
        CompleteTask("delete-all skipped no media found");
        return true;
    }

    // BUG-A-005: continue on individual delete failures; always write summary.
    snapshot_.outputs.clear();
    std::vector<std::string> failed_paths;
    for (const auto& remote_path : files) {
        if (!adapter_->DeleteMedia(remote_path)) {
            failed_paths.push_back(remote_path);
        } else {
            snapshot_.outputs.push_back(remote_path);
        }
    }

    snapshot_.completed_iterations = static_cast<int>(snapshot_.outputs.size());
    snapshot_.total_iterations = static_cast<int>(files.size());

    std::string summary_path;
    std::string error_message;
    if (!WriteDeleteAllSummaryFile(snapshot_.outputs, failed_paths, &summary_path, &error_message)) {
        FailTask(CameraTaskState::Failed, error_message);
        return false;
    }

    if (!failed_paths.empty()) {
        std::ostringstream oss;
        oss << "delete-all partial deleted=" << snapshot_.outputs.size()
            << " failed=" << failed_paths.size()
            << " summary_file=" << summary_path;
        if (count_ok) {
            oss << " reported_count=" << count;
        }
        CompleteTask(oss.str());
        return true;
    }

    std::ostringstream oss;
    oss << "deleted all media count=" << snapshot_.outputs.size()
        << " summary_file=" << summary_path;
    if (count_ok) {
        oss << " reported_count=" << count;
    }
    CompleteTask(oss.str());
    return true;
}

bool CameraController::DownloadCurrentRemoteFile() {
    if (adapter_ == nullptr || task_.download_index >= task_.pending_remote_urls.size()) {
        FailTask(CameraTaskState::Failed, "download index out of range");
        return false;
    }

    const std::string& remote_path = task_.pending_remote_urls[task_.download_index];
    const std::string local_path = JoinPath(task_.request.output_dir, FileNameFromUrl(remote_path));
    return FinalizeSingleDownload(remote_path, local_path);
}

bool CameraController::FinalizeSingleDownload(const std::string& remote_path,
                                              const std::string& local_path) {
    const std::string temp_path = local_path + ".part";
    const int download_timeout_ms = DownloadTimeoutMsForRemotePath(options_, remote_path);
    const auto started_at = SteadyClock::now();

    std::error_code ec;
    const bool part_existed = fs::exists(temp_path, ec);
    const bool file_existed = fs::exists(local_path, ec);
    const bool meta_existed = fs::exists(local_path + ".json", ec);
    // recovered=true when a previous download left residual state on disk
    const bool recovered = part_existed || (file_existed && !meta_existed);

    if (part_existed) {
        fprintf(stderr, "[CameraController] recovering interrupted download: removing stale .part"
                        " remote_path=%s\n", remote_path.c_str());
    } else if (file_existed && !meta_existed) {
        fprintf(stderr, "[CameraController] recovering incomplete download: file exists but"
                        " metadata missing, will overwrite remote_path=%s\n", remote_path.c_str());
    }

    fs::remove(temp_path, ec);

    std::string sdk_error_message;
    if (!adapter_->DownloadMedia(remote_path, temp_path, download_timeout_ms, &sdk_error_message)) {
        fs::remove(temp_path, ec);
        const auto elapsed = SteadyClock::now() - started_at;
        FailTask(CameraTaskState::Failed,
                 "failed to download remote_path=" + remote_path +
                     " local_temp_path=" + temp_path +
                     " timeout_ms=" + std::to_string(download_timeout_ms) +
                     " elapsed=" + FormatElapsedMs(elapsed) +
                     " sdk_error=" + (sdk_error_message.empty() ? "unknown" : sdk_error_message));
        return false;
    }

    fs::rename(temp_path, local_path, ec);
    if (ec) {
        fs::remove(temp_path, ec);
        FailTask(CameraTaskState::Failed,
                 "failed to finalize download remote_path=" + remote_path +
                     " local_temp_path=" + temp_path +
                     " local_final_path=" + local_path +
                     " rename_error=" + ec.message());
        return false;
    }

    if (!WriteMetadataFile(local_path, remote_path, recovered)) {
        FailTask(CameraTaskState::Failed,
                 "failed to write metadata remote_path=" + remote_path +
                     " local_final_path=" + local_path);
        return false;
    }

    snapshot_.outputs.push_back(local_path);
    return true;
}

bool CameraController::WriteMetadataFile(const std::string& local_path,
                                         const std::string& remote_path,
                                         bool recovered) {
    const uint64_t file_size = ComputeFileSize(local_path);
    if (user_data_store_ == nullptr) {
        return false;
    }

    const std::string serial_number = ActiveSerialNumber();
    std::ostringstream output;
    output << "{\n"
           << "  \"task_id\": " << task_.request.task_id << ",\n"
           << "  \"task_type\": \"" << JsonEscape(TaskTypeToString(task_.request.type)) << "\",\n"
           << "  \"serial\": \"" << JsonEscape(serial_number) << "\",\n"
           << "  \"remote_path\": \"" << JsonEscape(remote_path) << "\",\n"
           << "  \"local_path\": \"" << JsonEscape(local_path) << "\",\n"
           << "  \"file_size\": " << file_size << ",\n"
           << "  \"checksum_type\": \"disabled\",\n"
           << "  \"checksum\": \"\",\n"
           << "  \"recovered\": " << (recovered ? "true" : "false") << ",\n"
           << "  \"media_time\": " << snapshot_.runtime_status.media_time << ",\n"
           << "  \"created_at\": \"" << JsonEscape(NowTimestamp()) << "\"\n"
           << "}\n";

    std::string error_message;
    return user_data_store_->WriteTextFile(local_path + ".json", output.str(), &error_message);
}

bool CameraController::WriteLatestMediaListFile(const std::vector<std::string>& files,
                                                int reported_count,
                                                std::string* output_path,
                                                std::string* error_message) {
    if (user_data_store_ == nullptr) {
        if (error_message != nullptr) {
            *error_message = "user data store is null";
        }
        return false;
    }

    const std::string path = user_data_store_->LatestMediaListPath();
    std::ostringstream output;
    output << "# generated_at=" << NowTimestamp() << "\n";
    output << "# task_id=" << task_.request.task_id << "\n";
    output << "# reported_count=" << reported_count << "\n";
    for (const auto& file : files) {
        output << file << "\n";
    }
    if (!user_data_store_->WriteTextFile(path, output.str(), error_message)) {
        return false;
    }

    // BUG-A-006: also write a serial-agnostic fallback so commands issued without
    // camera_serial= can still resolve @LAST_MEDIA_FIRST via the no-suffix path.
    const std::string kGlobalFallbackTxt =
        FileSystemUserDataStore::BuildRuntimePath("camera_last_media_list", ".txt", "");
    if (path != kGlobalFallbackTxt) {
        std::string dummy_error;
        user_data_store_->WriteTextFile(kGlobalFallbackTxt, output.str(), &dummy_error);
    }

    if (output_path != nullptr) {
        *output_path = path;
    }
    return true;
}

bool CameraController::WriteLatestMediaListJsonFile(const std::vector<std::string>& files,
                                                    int reported_count,
                                                    std::string* output_path,
                                                    std::string* error_message) {
    if (user_data_store_ == nullptr) {
        if (error_message != nullptr) {
            *error_message = "user data store is null";
        }
        return false;
    }

    const std::string path = user_data_store_->LatestMediaListJsonPath();
    std::ostringstream output;
    output << "{\n"
           << "  \"task_id\": " << task_.request.task_id << ",\n"
           << "  \"task_type\": \"" << JsonEscape(TaskTypeToString(task_.request.type)) << "\",\n"
           << "  \"generated_at\": \"" << JsonEscape(NowTimestamp()) << "\",\n"
           << "  \"reported_count\": " << reported_count << ",\n"
           << "  \"listed_count\": " << files.size() << ",\n"
           << "  \"media_files\": [\n";
    for (std::size_t i = 0; i < files.size(); ++i) {
        output << "    {\n"
               << "      \"index\": " << i << ",\n"
               << "      \"remote_path\": \"" << JsonEscape(files[i]) << "\",\n"
               << "      \"file_name\": \"" << JsonEscape(FileNameFromUrl(files[i])) << "\"\n"
               << "    }";
        if (i + 1 != files.size()) {
            output << ",";
        }
        output << "\n";
    }
    output << "  ]\n"
           << "}\n";
    if (!user_data_store_->WriteTextFile(path, output.str(), error_message)) {
        return false;
    }

    // BUG-A-006: mirror to no-suffix fallback path for backward-compat downloads.
    const std::string kGlobalFallbackJson =
        FileSystemUserDataStore::BuildRuntimePath("camera_last_media_list", ".json", "");
    if (path != kGlobalFallbackJson) {
        std::string dummy_error;
        user_data_store_->WriteTextFile(kGlobalFallbackJson, output.str(), &dummy_error);
    }

    if (output_path != nullptr) {
        *output_path = path;
    }
    return true;
}

bool CameraController::WriteBatchDownloadSummaryFile(const std::vector<std::string>& local_paths,
                                                     std::string* output_path,
                                                     std::string* error_message) const {
    if (user_data_store_ == nullptr) {
        if (error_message != nullptr) {
            *error_message = "user data store is null";
        }
        return false;
    }

    const std::string path = JoinPath(
        task_.request.output_dir.empty()
            ? JoinPath(user_data_store_->DefaultOutputDir(options_), "batch")
            : task_.request.output_dir,
        "batch_download_summary.json");
    std::ostringstream output;
    output << "{\n"
           << "  \"task_id\": " << task_.request.task_id << ",\n"
           << "  \"task_type\": \"" << JsonEscape(TaskTypeToString(task_.request.type)) << "\",\n"
           << "  \"created_at\": \"" << JsonEscape(NowTimestamp()) << "\",\n"
           << "  \"downloaded_count\": " << local_paths.size() << ",\n"
           << "  \"local_files\": [\n";
    for (std::size_t i = 0; i < local_paths.size(); ++i) {
        output << "    \"" << JsonEscape(local_paths[i]) << "\"";
        if (i + 1 != local_paths.size()) {
            output << ",";
        }
        output << "\n";
    }
    output << "  ]\n"
           << "}\n";
    if (!user_data_store_->WriteTextFile(path, output.str(), error_message)) {
        return false;
    }

    if (output_path != nullptr) {
        *output_path = path;
    }
    return true;
}

bool CameraController::WriteDeleteAllSummaryFile(const std::vector<std::string>& deleted_paths,
                                                 const std::vector<std::string>& failed_paths,
                                                 std::string* output_path,
                                                 std::string* error_message) const {
    if (user_data_store_ == nullptr) {
        if (error_message != nullptr) {
            *error_message = "user data store is null";
        }
        return false;
    }

    const std::string path = user_data_store_->DeleteAllSummaryPath();
    const std::string serial_number = ActiveSerialNumber();
    const std::string status = failed_paths.empty() ? "ok" : "partial";
    std::ostringstream output;
    output << "{\n"
           << "  \"task_id\": " << task_.request.task_id << ",\n"
           << "  \"task_type\": \"" << JsonEscape(TaskTypeToString(task_.request.type)) << "\",\n"
           << "  \"serial\": \"" << JsonEscape(serial_number) << "\",\n"
           << "  \"created_at\": \"" << JsonEscape(NowTimestamp()) << "\",\n"
           << "  \"status\": \"" << status << "\",\n"
           << "  \"deleted_count\": " << deleted_paths.size() << ",\n"
           << "  \"failed_count\": " << failed_paths.size() << ",\n"
           << "  \"deleted_remote_paths\": [\n";
    for (std::size_t i = 0; i < deleted_paths.size(); ++i) {
        output << "    \"" << JsonEscape(deleted_paths[i]) << "\"";
        if (i + 1 != deleted_paths.size()) {
            output << ",";
        }
        output << "\n";
    }
    output << "  ],\n"
           << "  \"failed_remote_paths\": [\n";
    for (std::size_t i = 0; i < failed_paths.size(); ++i) {
        output << "    \"" << JsonEscape(failed_paths[i]) << "\"";
        if (i + 1 != failed_paths.size()) {
            output << ",";
        }
        output << "\n";
    }
    output << "  ]\n"
           << "}\n";
    if (!user_data_store_->WriteTextFile(path, output.str(), error_message)) {
        return false;
    }

    if (output_path != nullptr) {
        *output_path = path;
    }
    return true;
}

bool CameraController::WriteMediaTimeResultFile(int64_t media_time,
                                                std::string* output_path,
                                                std::string* error_message) const {
    if (user_data_store_ == nullptr) {
        if (error_message != nullptr) {
            *error_message = "user data store is null";
        }
        return false;
    }

    const std::string path = user_data_store_->LatestMediaTimeJsonPath();
    const std::string serial_number = ActiveSerialNumber();
    std::ostringstream output;
    output << "{\n"
           << "  \"task_id\": " << task_.request.task_id << ",\n"
           << "  \"task_type\": \"" << JsonEscape(TaskTypeToString(task_.request.type)) << "\",\n"
           << "  \"serial\": \"" << JsonEscape(serial_number) << "\",\n"
           << "  \"media_time\": " << media_time << ",\n"
           << "  \"created_at\": \"" << JsonEscape(NowTimestamp()) << "\"\n"
           << "}\n";
    if (!user_data_store_->WriteTextFile(path, output.str(), error_message)) {
        return false;
    }

    if (output_path != nullptr) {
        *output_path = path;
    }
    return true;
}

bool CameraController::WriteParamQueryResultFile(const std::string& json_body,
                                                 std::string* output_path,
                                                 std::string* error_message) const {
    if (user_data_store_ == nullptr) {
        if (error_message != nullptr) {
            *error_message = "user data store is null";
        }
        return false;
    }

    const std::string path = user_data_store_->LatestParamQueryJsonPath();
    const std::string serial_number = ActiveSerialNumber();
    std::ostringstream output;
    output << "{\n"
           << "  \"task_id\": " << task_.request.task_id << ",\n"
           << "  \"task_type\": \"" << JsonEscape(TaskTypeToString(task_.request.type)) << "\",\n"
           << "  \"serial\": \"" << JsonEscape(serial_number) << "\",\n"
           << "  \"created_at\": \"" << JsonEscape(NowTimestamp()) << "\",\n"
           << "  \"parameters\": " << json_body << "\n"
           << "}\n";
    if (!user_data_store_->WriteTextFile(path, output.str(), error_message)) {
        return false;
    }

    if (output_path != nullptr) {
        *output_path = path;
    }
    return true;
}

bool CameraController::WriteLogQueryResultFile(const std::string& log_url,
                                               std::string* output_path,
                                               std::string* error_message) const {
    if (user_data_store_ == nullptr) {
        if (error_message != nullptr) {
            *error_message = "user data store is null";
        }
        return false;
    }

    const std::string path = user_data_store_->LatestLogQueryJsonPath();
    const std::string serial_number = ActiveSerialNumber();
    std::ostringstream output;
    output << "{\n"
           << "  \"task_id\": " << task_.request.task_id << ",\n"
           << "  \"task_type\": \"" << JsonEscape(TaskTypeToString(task_.request.type)) << "\",\n"
           << "  \"serial\": \"" << JsonEscape(serial_number) << "\",\n"
           << "  \"log_url\": \"" << JsonEscape(log_url) << "\",\n"
           << "  \"created_at\": \"" << JsonEscape(NowTimestamp()) << "\"\n"
           << "}\n";
    if (!user_data_store_->WriteTextFile(path, output.str(), error_message)) {
        return false;
    }

    if (output_path != nullptr) {
        *output_path = path;
    }
    return true;
}

bool CameraController::WriteDeviceListResultFile(const std::vector<CameraInfo>& cameras,
                                                 std::string* output_path,
                                                 std::string* error_message) const {
    if (user_data_store_ == nullptr) {
        if (error_message != nullptr) {
            *error_message = "user data store is null";
        }
        return false;
    }

    const auto* file_store = dynamic_cast<const FileSystemUserDataStore*>(user_data_store_);
    if (file_store == nullptr) {
        if (error_message != nullptr) {
            *error_message = "camera device list requires filesystem-backed user data store";
        }
        return false;
    }

    const std::string path = file_store->CameraDeviceListJsonPath();
    std::ostringstream output;
    output << "{\n"
           << "  \"task_id\": " << task_.request.task_id << ",\n"
           << "  \"task_type\": \"" << JsonEscape(TaskTypeToString(task_.request.type)) << "\",\n"
           << "  \"generated_at\": \"" << JsonEscape(NowTimestamp()) << "\",\n"
           << "  \"camera_count\": " << cameras.size() << ",\n"
           << "  \"cameras\": [\n";
    for (std::size_t i = 0; i < cameras.size(); ++i) {
        output << "    {\n"
               << "      \"camera_id\": \"cam" << i << "\",\n"
               << "      \"serial\": \"" << JsonEscape(cameras[i].serial_number) << "\",\n"
               << "      \"camera_name\": \"" << JsonEscape(cameras[i].camera_name) << "\",\n"
               << "      \"firmware_version\": \"" << JsonEscape(cameras[i].firmware_version)
               << "\",\n"
               << "      \"camera_type\": \"" << JsonEscape(cameras[i].model_key) << "\",\n"
               << "      \"model_key\": \"" << JsonEscape(cameras[i].model_key) << "\",\n"
               << "      \"capabilities\": \"" << JsonEscape(cameras[i].capabilities)
               << "\"\n"
               << "    }";
        if (i + 1 != cameras.size()) {
            output << ",";
        }
        output << "\n";
    }
    output << "  ]\n"
           << "}\n";
    if (!user_data_store_->WriteTextFile(path, output.str(), error_message)) {
        return false;
    }

    if (output_path != nullptr) {
        *output_path = path;
    }
    return true;
}

bool CameraController::LoadLatestMediaList(std::vector<std::string>* files,
                                           std::string* error_message) const {
    if (files == nullptr || user_data_store_ == nullptr) {
        if (error_message != nullptr) {
            *error_message = files == nullptr ? "latest media list output is null"
                                              : "user data store is null";
        }
        return false;
    }

    std::vector<std::string> raw_lines;
    if (!user_data_store_->ReadLines(user_data_store_->LatestMediaListPath(), &raw_lines,
                                     error_message)) {
        return false;
    }
    files->clear();
    for (const auto& line : raw_lines) {
        const std::string trimmed = line.empty() ? std::string() : line;
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        files->push_back(trimmed);
    }

    return true;
}

CameraRuntimeStatus CameraController::CollectRuntimeStatus() const {
    CameraRuntimeStatus status = snapshot_.runtime_status;
    if (adapter_ == nullptr || !adapter_->IsConnected()) {
        status.connected = false;
        status.capturing = false;
        status.battery_ok = false;
        status.storage_ok = false;
        status.media_time_ok = false;
        status.recent_error = "camera_not_connected";
        return status;
    }

    status.connected = true;
    // CAM-004: in worker mode, high-frequency background SDK status polling can stall the
    // camera loop right after a successful Open(). Keep the always-on heartbeat minimal so
    // command handling and recording remain available; richer queries still use explicit tasks.
    status.capturing = false;
    status.recent_error.clear();
    return status;
}

std::string CameraController::ActiveSerialNumber() const {
    if (adapter_ != nullptr && !adapter_->CurrentSerialNumber().empty()) {
        return adapter_->CurrentSerialNumber();
    }
    if (!task_.request.target_camera_serial.empty()) {
        return task_.request.target_camera_serial;
    }
    return options_.serial_number;
}

std::string CameraController::JoinPath(const std::string& base_dir, const std::string& file_name) {
    return (fs::path(base_dir) / file_name).string();
}

std::string CameraController::FileNameFromUrl(const std::string& url) {
    const auto end = url.find_first_of("?#");
    const auto path = url.substr(0, end);
    const auto slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

std::string CameraController::NormalizeTimeFilterToken(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    std::string normalized;
    normalized.reserve(14);
    for (char ch : value) {
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            normalized.push_back(ch);
            continue;
        }
        if (ch == '_' || ch == '-' || ch == ':' || ch == 'T' || ch == ' ') {
            continue;
        }
        return {};
    }

    if (normalized.size() != 14) {
        return {};
    }
    return normalized;
}

uint64_t CameraController::ComputeFileSize(const std::string& path) {
    try {
        return static_cast<uint64_t>(fs::file_size(path));
    } catch (const std::exception&) {
        return 0;
    }
}

std::string CameraController::ComputeFNV1a64(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }

    constexpr uint64_t kOffset = 14695981039346656037ull;
    constexpr uint64_t kPrime = 1099511628211ull;
    uint64_t hash = kOffset;

    char buffer[4096];
    while (input.good()) {
        input.read(buffer, sizeof(buffer));
        const std::streamsize count = input.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<unsigned char>(buffer[i]);
            hash *= kPrime;
        }
    }

    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return oss.str();
}

std::string CameraController::JsonEscape(const std::string& value) {
    std::ostringstream oss;
    for (char ch : value) {
        switch (ch) {
        case '\\':
            oss << "\\\\";
            break;
        case '"':
            oss << "\\\"";
            break;
        case '\n':
            oss << "\\n";
            break;
        case '\r':
            oss << "\\r";
            break;
        case '\t':
            oss << "\\t";
            break;
        default:
            oss << ch;
            break;
        }
    }
    return oss.str();
}

int CameraController::NextTaskId() {
    static std::atomic<int> next_id{1};
    return next_id.fetch_add(1);
}

}  // namespace youyeetoo
