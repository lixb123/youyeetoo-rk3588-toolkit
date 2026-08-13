#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "camera_app.h"

namespace ins_camera {
class Camera;
}

namespace youyeetoo {

struct CaptureResult {
    std::vector<std::string> origin_urls;
    std::vector<std::string> low_resolution_urls;
};

class CameraSdkAdapter {
public:
    CameraSdkAdapter();
    ~CameraSdkAdapter();

    void ConfigureLogging(bool debug, const std::string& log_path);
    std::vector<CameraInfo> ListCameras();
    bool OpenCamera(const std::string& serial_number = std::string(), int timeout_ms = 10000);
    void CloseCamera();
    bool IsConnected() const;
    bool CaptureCurrentStatus() const;
    const std::string& CurrentSerialNumber() const;
    const std::string& CurrentModelKey() const;
    CameraInfo CurrentCameraInfo() const;

    bool GetBatteryStatus(BatteryInfo* status);
    bool GetStorageState(StorageInfo* status);
    int64_t GetCameraMediaTime() const;
    std::string GetCameraLogFileUrl() const;

    bool SetNormalPhotoMode();
    bool SetCaptureMode(const std::string& mode, std::string* error_message);
    bool SetCaptureParameters(const std::map<std::string, std::string>& parameters,
                              std::string* summary,
                              std::string* error_message);
    bool GetCaptureParameters(const std::map<std::string, std::string>& parameters,
                              std::string* json_body,
                              std::string* summary,
                              std::string* error_message) const;
    CaptureResult TakePhoto();
    bool TakePhotoAndDownload(const std::string& output_dir, std::vector<std::string>* local_paths);

    bool SetNormalVideoMode();
    bool StartRecording(const std::map<std::string, std::string>& parameters,
                        std::string* summary,
                        std::string* error_message);
    CaptureResult StopRecording();
    bool StartExtendedCapture(const std::string& mode, std::string* error_message);
    CaptureResult StopExtendedCapture(const std::string& mode, std::string* error_message);
    bool GetRecordingFiles(std::vector<std::string>* file_list);

    std::vector<std::string> ListMedia();
    bool GetMediaCount(int* count);
    bool StartPreview(const std::map<std::string, std::string>& parameters,
                      std::string* summary,
                      std::string* error_message);
    bool StopPreview(std::string* summary, std::string* error_message);
    bool ShutdownForMaintenance(std::string* error_message);
    bool DownloadMedia(const std::string& remote_path,
                       const std::string& local_path,
                       int timeout_ms,
                       std::string* error_message);
    bool DeleteMedia(const std::string& remote_path);

private:
    enum class ExtendedCaptureProfile {
        Unsupported,
        Standard57K,
        X4Air6K,
    };
    static std::string FileNameFromUrl(const std::string& url);
    void ApplyTimeout(int timeout_ms);
    // BUG-A-004: HTTP fallback for video files that DownloadCameraFile rejects.
    static bool HttpDownloadFile(const std::string& http_base_url,
                                 const std::string& remote_path,
                                 const std::string& local_path,
                                 int timeout_ms,
                                 std::string* error_message);
    std::shared_ptr<ins_camera::Camera> camera_;
    bool is_x4_air_ = false;
    ExtendedCaptureProfile extended_capture_profile_ = ExtendedCaptureProfile::Unsupported;
    std::string current_model_key_ = "unknown";
    std::string current_capabilities_ = "status,capture,battery,storage,media,photo,video";
    int timeout_ms_ = 10000;
    bool preview_active_ = false;
    std::string last_mode_hint_ = "unknown";
    std::string current_serial_number_;
    std::string current_camera_name_;
    std::string current_firmware_version_;
};

}  // namespace youyeetoo
