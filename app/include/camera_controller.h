#pragma once

#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "camera_app.h"

namespace youyeetoo {

class CameraSdkAdapter;
class UserDataStore;

enum class CameraDeviceState {
    Unavailable,
    Connecting,
    Idle,
    Busy,
    ErrorRecover,
    Fatal,
};

enum class CameraTaskType {
    None,
    AutoPhotoSequence,
    TakePhotoDownload,
    SetMode,
    SetParam,
    GetParam,
    GetMediaTime,
    GetLog,
    ListDevices,
    PreviewStart,
    PreviewStop,
    ShutdownMaint,
    StartRecording,
    StopRecording,
    StartExtendedCapture,
    StopExtendedCapture,
    ListMedia,
    DownloadMedia,
    BatchDownload,
    DeleteMedia,
    DeleteAllMedia,
    Initialize,
    GetStatus,
    GetCaptureStatus,
    GetBattery,
    GetStorage,
};

enum class CameraTaskState {
    None,
    Queued,
    Precheck,
    Capturing,
    Configuring,
    Recording,
    StoppingRecord,
    Listing,
    Downloading,
    Deleting,
    Verifying,
    Completed,
    Failed,
    Timeout,
    Cancelled,
};

struct CameraTaskRequest {
    CameraTaskType type = CameraTaskType::None;
    int task_id = 0;
    int repeat_count = 1;
    std::string output_dir;
    std::string remote_path;
    std::string local_path;
    std::string extended_mode;
    std::string batch_time_from;
    std::string batch_time_to;
    std::string target_camera_serial;
    std::map<std::string, std::string> parameters;
    bool delete_confirmed = false;
};

struct CameraControllerSnapshot {
    CameraDeviceState device_state = CameraDeviceState::Unavailable;
    CameraTaskType task_type = CameraTaskType::None;
    CameraTaskState task_state = CameraTaskState::None;
    CameraRuntimeStatus runtime_status;
    int recover_attempts = 0;
    int completed_iterations = 0;
    int total_iterations = 0;
    std::vector<std::string> outputs;
    std::string result_summary;
    std::string error_message;
};

class CameraController {
public:
    CameraController(CameraSdkAdapter* adapter, UserDataStore* user_data_store);

    void Configure(const CameraAppOptions& options);
    bool StartTask(const CameraTaskRequest& request);
    void CancelTask(const std::string& reason);
    void Tick();

    const CameraControllerSnapshot& snapshot() const;
    bool HasActiveTask() const;
    bool IsTaskFinished() const;

    static std::string DeviceStateToString(CameraDeviceState state);
    static std::string TaskTypeToString(CameraTaskType type);
    static std::string TaskStateToString(CameraTaskState state);

private:
    struct TaskContext {
        CameraTaskRequest request;
        std::vector<std::string> pending_remote_urls;
        std::size_t download_index = 0;
        std::chrono::steady_clock::time_point state_since = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now();
    };

    void ResetTask();
    void SetDeviceState(CameraDeviceState state);
    void SetTaskState(CameraTaskState state);
    void FailTask(CameraTaskState state, const std::string& error_message);
    void CompleteTask(const std::string& summary);

    void TickConnection(std::chrono::steady_clock::time_point now);
    void TickStatusPoll(std::chrono::steady_clock::time_point now);
    void TickTask(std::chrono::steady_clock::time_point now);

    bool BeginPhotoTask(std::chrono::steady_clock::time_point now);
    bool ContinuePhotoTask(std::chrono::steady_clock::time_point now);
    bool BeginSetModeTask();
    bool BeginSetParamTask();
    bool BeginGetParamTask();
    bool BeginGetMediaTimeTask();
    bool BeginGetLogTask();
    bool BeginListDevicesTask();
    bool BeginInitializeTask();
    bool BeginGetStatusTask();
    bool BeginGetBatteryTask();
    bool BeginGetStorageTask();
    bool BeginPreviewStartTask();
    bool BeginPreviewStopTask();
    bool BeginShutdownMaintTask();
    bool BeginRecordStartTask();
    bool BeginRecordStopTask();
    bool BeginExtendedCaptureStartTask();
    bool BeginExtendedCaptureStopTask();
    bool BeginListMediaTask();
    bool BeginDownloadMediaTask();
    bool BeginBatchDownloadTask();
    bool BeginDeleteMediaTask();
    bool BeginDeleteAllMediaTask();
    bool DownloadCurrentRemoteFile();
    bool WriteMetadataFile(const std::string& local_path, const std::string& remote_path,
                           bool recovered);
    bool WriteLatestMediaListFile(const std::vector<std::string>& files,
                                  int reported_count,
                                  std::string* output_path,
                                  std::string* error_message);
    bool WriteLatestMediaListJsonFile(const std::vector<std::string>& files,
                                      int reported_count,
                                      std::string* output_path,
                                      std::string* error_message);
    bool WriteBatchDownloadSummaryFile(const std::vector<std::string>& local_paths,
                                       std::string* output_path,
                                       std::string* error_message) const;
    bool WriteDeleteAllSummaryFile(const std::vector<std::string>& deleted_paths,
                                   const std::vector<std::string>& failed_paths,
                                   std::string* output_path,
                                   std::string* error_message) const;
    bool WriteMediaTimeResultFile(int64_t media_time,
                                  std::string* output_path,
                                  std::string* error_message) const;
    bool WriteParamQueryResultFile(const std::string& json_body,
                                   std::string* output_path,
                                   std::string* error_message) const;
    bool WriteLogQueryResultFile(const std::string& log_url,
                                 std::string* output_path,
                                 std::string* error_message) const;
    bool WriteDeviceListResultFile(const std::vector<CameraInfo>& cameras,
                                   std::string* output_path,
                                   std::string* error_message) const;
    bool FinalizeSingleDownload(const std::string& remote_path, const std::string& local_path);
    bool LoadLatestMediaList(std::vector<std::string>* files, std::string* error_message) const;

    CameraRuntimeStatus CollectRuntimeStatus() const;
    std::string ActiveSerialNumber() const;

    static std::string JoinPath(const std::string& base_dir, const std::string& file_name);
    static std::string FileNameFromUrl(const std::string& url);
    static std::string NormalizeTimeFilterToken(const std::string& value);
    static uint64_t ComputeFileSize(const std::string& path);
    static std::string ComputeFNV1a64(const std::string& path);
    static std::string JsonEscape(const std::string& value);
    static int NextTaskId();

    CameraSdkAdapter* adapter_ = nullptr;
    UserDataStore* user_data_store_ = nullptr;
    CameraAppOptions options_;
    CameraControllerSnapshot snapshot_;
    TaskContext task_;
    bool has_task_ = false;
    std::chrono::steady_clock::time_point next_poll_time_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point next_reconnect_time_ = std::chrono::steady_clock::now();
};

}  // namespace youyeetoo
