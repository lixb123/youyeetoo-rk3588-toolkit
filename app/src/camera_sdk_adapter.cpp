#include "camera_sdk_adapter.h"

#include <camera/camera.h>
#include <camera/device_discovery.h>
#include <camera/photography_settings.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

// POSIX socket headers for HTTP fallback download (BUG-A-004)
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace youyeetoo {
namespace {

std::string CameraTypeToModelKey(ins_camera::CameraType type) {
    switch (type) {
    case ins_camera::CameraType::Insta360OneX: return "Insta360OneX";
    case ins_camera::CameraType::Insta360OneR: return "Insta360OneR";
    case ins_camera::CameraType::Insta360OneRS: return "Insta360OneRS";
    case ins_camera::CameraType::Insta360OneX2: return "Insta360OneX2";
    case ins_camera::CameraType::Insta360X3: return "Insta360X3";
    case ins_camera::CameraType::Insta360X4: return "Insta360X4";
    case ins_camera::CameraType::Insta360X5: return "Insta360X5";
    case ins_camera::CameraType::Insta360X4Air: return "Insta360X4Air";
    case ins_camera::CameraType::Unknown: return "Unknown";
    }
    return "Unknown";
}

std::string CameraCapabilities(ins_camera::CameraType type) {
    // Basic status, capture, battery, storage and media operations are common
    // to every model exposed by CameraSDK. Extended modes are model-dependent.
    std::string capabilities = "status,capture,battery,storage,media,photo,video";
    if (type == ins_camera::CameraType::Insta360X5) {
        capabilities += ",purevideo,timeshift,hdrvideo";
    } else if (type == ins_camera::CameraType::Insta360X4Air) {
        capabilities += ",timeshift,hdrvideo";
    }
    return capabilities;
}

std::string PowerTypeToString(ins_camera::PowerType power_type) {
    switch (power_type) {
    case ins_camera::PowerType::BATTERY:
        return "battery";
    case ins_camera::PowerType::ADAPTER:
        return "adapter";
    }
    return "unknown";
}

std::string StorageStateToString(ins_camera::CardState state) {
    switch (state) {
    case ins_camera::CardState::STOR_CS_PASS:
        return "pass";
    case ins_camera::CardState::STOR_CS_NOCARD:
        return "no_card";
    case ins_camera::CardState::STOR_CS_NOSPACE:
        return "no_space";
    case ins_camera::CardState::STOR_CS_INVALID_FORMAT:
        return "invalid_format";
    case ins_camera::CardState::STOR_CS_WPCARD:
        return "write_protected";
    case ins_camera::CardState::STOR_CS_OTHER_ERROR:
        return "other_error";
    }
    return "unknown";
}

CaptureResult ToCaptureResult(const ins_camera::MediaUrl& media_url) {
    if (media_url.Empty()) {
        return {};
    }

    return CaptureResult{
        media_url.OriginUrls(),
        media_url.LRVUrls(),
    };
}

bool ParseInt(const std::string& value, int32_t* output) {
    if (output == nullptr) {
        return false;
    }
    try {
        *output = std::stoi(value);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool ParseDouble(const std::string& value, double* output) {
    if (output == nullptr) {
        return false;
    }
    try {
        *output = std::stod(value);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string ToLowerCopy(const std::string& value) {
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

bool ParseBool(const std::string& value, bool* output) {
    if (output == nullptr) {
        return false;
    }
    const std::string normalized = ToLowerCopy(value);
    if (normalized == "1" || normalized == "true" || normalized == "on" || normalized == "yes") {
        *output = true;
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "off" || normalized == "no") {
        *output = false;
        return true;
    }
    return false;
}

bool ParsePhotoMode(const std::string& mode, ins_camera::SubPhotoMode* output) {
    if (output == nullptr) {
        return false;
    }
    const std::string normalized = ToLowerCopy(mode);
    if (normalized == "photo" || normalized == "photo_single") {
        *output = ins_camera::SubPhotoMode::PHOTO_SINGLE;
        return true;
    }
    if (normalized == "photo_hdr" || normalized == "hdr_photo") {
        *output = ins_camera::SubPhotoMode::PHOTO_HDR;
        return true;
    }
    if (normalized == "photo_interval" || normalized == "interval_photo") {
        *output = ins_camera::SubPhotoMode::PHOTO_INTERVAL;
        return true;
    }
    if (normalized == "photo_burst" || normalized == "burst_photo") {
        *output = ins_camera::SubPhotoMode::PHOTO_BURST;
        return true;
    }
    return false;
}

bool ParseVideoMode(const std::string& mode, ins_camera::SubVideoMode* output) {
    if (output == nullptr) {
        return false;
    }
    const std::string normalized = ToLowerCopy(mode);
    if (normalized == "video" || normalized == "video_normal") {
        *output = ins_camera::SubVideoMode::VIDEO_NORMAL;
        return true;
    }
    if (normalized == "video_hdr" || normalized == "hdrvideo") {
        *output = ins_camera::SubVideoMode::VIDEO_HDR;
        return true;
    }
    if (normalized == "video_timelapse" || normalized == "timelapse") {
        *output = ins_camera::SubVideoMode::VIDEO_TIMELAPSE;
        return true;
    }
    if (normalized == "video_timeshift" || normalized == "timeshift") {
        *output = ins_camera::SubVideoMode::VIDEO_TIMESHIFT;
        return true;
    }
    if (normalized == "video_pure" || normalized == "purevideo") {
        *output = ins_camera::SubVideoMode::VIDEO_PURE;
        return true;
    }
    return false;
}

bool ParseExposureMode(const std::string& value,
                       ins_camera::PhotographyOptions_ExposureMode* output) {
    if (output == nullptr) {
        return false;
    }
    const std::string normalized = ToLowerCopy(value);
    if (normalized == "auto") {
        *output = ins_camera::PhotographyOptions_ExposureMode::AUTO;
        return true;
    }
    if (normalized == "iso_priority") {
        *output = ins_camera::PhotographyOptions_ExposureMode::ISO_PRIORITY;
        return true;
    }
    if (normalized == "shutter_priority") {
        *output = ins_camera::PhotographyOptions_ExposureMode::SHUTTER_PRIORITY;
        return true;
    }
    if (normalized == "manual") {
        *output = ins_camera::PhotographyOptions_ExposureMode::MANUAL;
        return true;
    }
    if (normalized == "adaptive") {
        *output = ins_camera::PhotographyOptions_ExposureMode::ADAPTIVE;
        return true;
    }
    if (normalized == "full_auto") {
        *output = ins_camera::PhotographyOptions_ExposureMode::FULL_AUTO;
        return true;
    }
    return false;
}

bool ParseWhiteBalance(const std::string& value,
                       ins_camera::PhotographyOptions_WhiteBalance* output) {
    if (output == nullptr) {
        return false;
    }
    const std::string normalized = ToLowerCopy(value);
    if (normalized == "auto") {
        *output = ins_camera::PhotographyOptions_WhiteBalance::WB_AUTO;
        return true;
    }
    if (normalized == "2700" || normalized == "2700k") {
        *output = ins_camera::PhotographyOptions_WhiteBalance::WB_2700K;
        return true;
    }
    if (normalized == "4000" || normalized == "4000k") {
        *output = ins_camera::PhotographyOptions_WhiteBalance::WB_4000K;
        return true;
    }
    if (normalized == "5000" || normalized == "5000k") {
        *output = ins_camera::PhotographyOptions_WhiteBalance::WB_5000K;
        return true;
    }
    if (normalized == "6500" || normalized == "6500k") {
        *output = ins_camera::PhotographyOptions_WhiteBalance::WB_6500K;
        return true;
    }
    if (normalized == "7500" || normalized == "7500k") {
        *output = ins_camera::PhotographyOptions_WhiteBalance::WB_7500K;
        return true;
    }
    return false;
}

bool ParseGlobalSharpness(const std::string& value, ins_camera::SharpnessLevel* output) {
    if (output == nullptr) {
        return false;
    }
    const std::string normalized = ToLowerCopy(value);
    if (normalized == "low" || normalized == "0") {
        *output = ins_camera::SharpnessLevel::Low;
        return true;
    }
    if (normalized == "medium" || normalized == "1") {
        *output = ins_camera::SharpnessLevel::Medium;
        return true;
    }
    if (normalized == "high" || normalized == "2") {
        *output = ins_camera::SharpnessLevel::High;
        return true;
    }
    return false;
}

bool ParseSensorDevice(const std::string& value, ins_camera::SensorDevice* output) {
    if (output == nullptr) {
        return false;
    }
    const std::string normalized = ToLowerCopy(value);
    if (normalized == "front" || normalized == "wide") {
        *output = ins_camera::SENSOR_DEVICE_FRONT;
        return true;
    }
    if (normalized == "rear" || normalized == "pano") {
        *output = ins_camera::SENSOR_DEVICE_REAR;
        return true;
    }
    if (normalized == "all" || normalized == "both") {
        *output = ins_camera::SENSOR_DEVICE_ALL;
        return true;
    }
    return false;
}

bool ParseNormalVideoResolution(const std::string& value, ins_camera::VideoResolution* output) {
    if (output == nullptr) {
        return false;
    }
    const std::string normalized = ToLowerCopy(value);
    if (normalized == "8k30" || normalized == "8kp30") {
        *output = ins_camera::VideoResolution::RES_8KP30;
        return true;
    }
    if (normalized == "8k25" || normalized == "8kp25") {
        *output = ins_camera::VideoResolution::RES_8KP25;
        return true;
    }
    if (normalized == "8k24" || normalized == "8kp24") {
        *output = ins_camera::VideoResolution::RES_8KP24;
        return true;
    }
    if (normalized == "57k30" || normalized == "5.7k30" || normalized == "57kp30") {
        *output = ins_camera::VideoResolution::RES_57KP30;
        return true;
    }
    if (normalized == "57k25" || normalized == "5.7k25" || normalized == "57kp25") {
        *output = ins_camera::VideoResolution::RES_57KP25;
        return true;
    }
    if (normalized == "57k24" || normalized == "5.7k24" || normalized == "57kp24") {
        *output = ins_camera::VideoResolution::RES_57KP24;
        return true;
    }
    if (normalized == "4k30" || normalized == "4kp30") {
        *output = ins_camera::VideoResolution::RES_3840_1920P30;
        return true;
    }
    if (normalized == "4k25" || normalized == "4kp25") {
        *output = ins_camera::VideoResolution::RES_3840_1920P25;
        return true;
    }
    if (normalized == "4k24" || normalized == "4kp24") {
        *output = ins_camera::VideoResolution::RES_3840_1920P24;
        return true;
    }
    return false;
}

std::string NormalVideoResolutionToSummaryString(ins_camera::VideoResolution resolution) {
    switch (resolution) {
    case ins_camera::VideoResolution::RES_8KP30:
        return "8k30";
    case ins_camera::VideoResolution::RES_8KP25:
        return "8k25";
    case ins_camera::VideoResolution::RES_8KP24:
        return "8k24";
    case ins_camera::VideoResolution::RES_57KP30:
        return "5.7k30";
    case ins_camera::VideoResolution::RES_57KP25:
        return "5.7k25";
    case ins_camera::VideoResolution::RES_57KP24:
        return "5.7k24";
    case ins_camera::VideoResolution::RES_3840_1920P30:
        return "4k30";
    case ins_camera::VideoResolution::RES_3840_1920P25:
        return "4k25";
    case ins_camera::VideoResolution::RES_3840_1920P24:
        return "4k24";
    default:
        return "custom";
    }
}

bool ParsePreviewResolution(const std::string& value, ins_camera::VideoResolution* output) {
    if (output == nullptr) {
        return false;
    }
    const std::string normalized = ToLowerCopy(value);
    if (normalized == "1080p30" || normalized == "1920x1080p30") {
        *output = ins_camera::VideoResolution::RES_1920_1080P30;
        return true;
    }
    if (normalized == "1440p30" || normalized == "2560x1440p30") {
        *output = ins_camera::VideoResolution::RES_2560_1440P30;
        return true;
    }
    if (normalized == "960p30" || normalized == "1920x960p30") {
        *output = ins_camera::VideoResolution::RES_1920_960P30;
        return true;
    }
    if (normalized == "360p30" || normalized == "640x360p30") {
        *output = ins_camera::VideoResolution::RES_640_360P30;
        return true;
    }
    return false;
}

ins_camera::CameraFunctionMode ParseScope(const std::map<std::string, std::string>& parameters) {
    const auto scope_it = parameters.find("scope");
    if (scope_it == parameters.end()) {
        return ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_VIDEO;
    }
    const std::string normalized = ToLowerCopy(scope_it->second);
    if (normalized == "photo") {
        return ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_IMAGE;
    }
    return ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_VIDEO;
}

std::string ExposureModeToString(ins_camera::PhotographyOptions_ExposureMode mode) {
    switch (mode) {
    case ins_camera::PhotographyOptions_ExposureMode::AUTO:
        return "auto";
    case ins_camera::PhotographyOptions_ExposureMode::ISO_PRIORITY:
        return "iso_priority";
    case ins_camera::PhotographyOptions_ExposureMode::SHUTTER_PRIORITY:
        return "shutter_priority";
    case ins_camera::PhotographyOptions_ExposureMode::MANUAL:
        return "manual";
    case ins_camera::PhotographyOptions_ExposureMode::ADAPTIVE:
        return "adaptive";
    case ins_camera::PhotographyOptions_ExposureMode::FULL_AUTO:
        return "full_auto";
    }
    return "unknown";
}

std::string WhiteBalanceToString(ins_camera::PhotographyOptions_WhiteBalance wb) {
    switch (wb) {
    case ins_camera::PhotographyOptions_WhiteBalance::WB_UNKNOWN:
        return "unknown";
    case ins_camera::PhotographyOptions_WhiteBalance::WB_AUTO:
        return "auto";
    case ins_camera::PhotographyOptions_WhiteBalance::WB_2700K:
        return "2700k";
    case ins_camera::PhotographyOptions_WhiteBalance::WB_4000K:
        return "4000k";
    case ins_camera::PhotographyOptions_WhiteBalance::WB_5000K:
        return "5000k";
    case ins_camera::PhotographyOptions_WhiteBalance::WB_6500K:
        return "6500k";
    case ins_camera::PhotographyOptions_WhiteBalance::WB_7500K:
        return "7500k";
    }
    return "unknown";
}

std::string SharpnessLevelToString(ins_camera::SharpnessLevel value) {
    switch (value) {
    case ins_camera::SharpnessLevel::Low:
        return "low";
    case ins_camera::SharpnessLevel::Medium:
        return "medium";
    case ins_camera::SharpnessLevel::High:
        return "high";
    }
    return "unknown";
}

std::string LensTypeToString(ins_camera::CameraLensType value) {
    switch (value) {
    case ins_camera::CameraLensType::PanoDefault:
        return "pano_default";
    case ins_camera::CameraLensType::Wide577:
        return "wide577";
    case ins_camera::CameraLensType::Pano577:
        return "pano577";
    case ins_camera::CameraLensType::Wide283:
        return "wide283";
    case ins_camera::CameraLensType::Pano283:
        return "pano283";
    case ins_camera::CameraLensType::Wide586:
        return "wide586";
    case ins_camera::CameraLensType::Pano586:
        return "pano586";
    case ins_camera::CameraLensType::Action577:
        return "action577";
    }
    return "unknown";
}

std::string JsonEscape(const std::string& value) {
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

}  // namespace

CameraSdkAdapter::CameraSdkAdapter() = default;

CameraSdkAdapter::~CameraSdkAdapter() {
    CloseCamera();
}

void CameraSdkAdapter::ConfigureLogging(bool debug, const std::string& log_path) {
    ins_camera::SetLogLevel(debug ? ins_camera::LogLevel::VERBOSE
                                  : ins_camera::LogLevel::ERR);

    if (!log_path.empty()) {
        ins_camera::SetLogPath(log_path);
    }
}

std::vector<CameraInfo> CameraSdkAdapter::ListCameras() {
    ins_camera::DeviceDiscovery discovery;
    auto devices = discovery.GetAvailableDevices();

    std::vector<CameraInfo> cameras;
    cameras.reserve(devices.size() + 1);  // +1 for possible self-supplement

    for (const auto& device : devices) {
        cameras.push_back(CameraInfo{
            device.serial_number,
            device.camera_name,
            device.fw_version,
            CameraTypeToModelKey(device.camera_type),
            CameraCapabilities(device.camera_type),
        });
    }

    discovery.FreeDeviceDescriptors(devices);

    // BUG-A-007: DeviceDiscovery::GetAvailableDevices() does a brief Open() on each USB
    // device to read its serial/name/fw.  A camera that is already held open by this
    // adapter (camera_ != nullptr) will reject the second Open() and be silently dropped
    // from the enumeration result.  Re-add it explicitly so the caller gets a complete
    // picture of all physically connected cameras regardless of session state.
    if (camera_ != nullptr && !current_serial_number_.empty()) {
        const bool already_listed = std::any_of(
            cameras.begin(), cameras.end(),
            [this](const CameraInfo& c) {
                return c.serial_number == current_serial_number_;
            });
        if (!already_listed) {
            cameras.push_back(CameraInfo{
                current_serial_number_,
                "(active-session)",
                "(active-session)",
                current_model_key_,
                current_capabilities_,
            });
        }
    }

    return cameras;
}

bool CameraSdkAdapter::OpenCamera(const std::string& serial_number, int timeout_ms) {
    CloseCamera();

    ins_camera::DeviceDiscovery discovery;
    auto devices = discovery.GetAvailableDevices();
    std::cout << "[CameraSdkAdapter] GetAvailableDevices count=" << devices.size()
              << " requested_serial=" << (serial_number.empty() ? "(any)" : serial_number)
              << std::endl;
    for (const auto& device : devices) {
        std::cout << "[CameraSdkAdapter] candidate serial=" << device.serial_number
                  << " name=" << device.camera_name
                  << " model=" << CameraTypeToModelKey(device.camera_type)
                  << " fw=" << device.fw_version
                  << std::endl;
    }
    if (devices.empty()) {
        std::cerr << "[CameraSdkAdapter] no camera devices discovered" << std::endl;
        return false;
    }

    const auto selected = std::find_if(
        devices.begin(),
        devices.end(),
        [&serial_number](const ins_camera::DeviceDescriptor& device) {
            return serial_number.empty() || device.serial_number == serial_number;
        });

    if (selected == devices.end()) {
        std::cerr << "[CameraSdkAdapter] requested serial not found: "
                  << (serial_number.empty() ? "(any)" : serial_number) << std::endl;
        discovery.FreeDeviceDescriptors(devices);
        return false;
    }

    std::cout << "[CameraSdkAdapter] opening serial=" << selected->serial_number
              << " timeout_ms=" << timeout_ms << std::endl;
    camera_ = std::make_shared<ins_camera::Camera>(selected->info);
    is_x4_air_ = selected->camera_type == ins_camera::CameraType::Insta360X4Air;
    if (selected->camera_type == ins_camera::CameraType::Insta360X5) {
        extended_capture_profile_ = ExtendedCaptureProfile::Standard57K;
    } else if (is_x4_air_) {
        extended_capture_profile_ = ExtendedCaptureProfile::X4Air6K;
    } else {
        extended_capture_profile_ = ExtendedCaptureProfile::Unsupported;
    }
    current_model_key_ = CameraTypeToModelKey(selected->camera_type);
    current_capabilities_ = CameraCapabilities(selected->camera_type);
    current_serial_number_ = selected->serial_number;
    current_camera_name_ = selected->camera_name;
    current_firmware_version_ = selected->fw_version;
    timeout_ms_ = timeout_ms;
    camera_->SetTimeout(timeout_ms_);
    const bool opened = camera_->Open();
    const bool connected_after_open = camera_ && camera_->IsConnected();

    discovery.FreeDeviceDescriptors(devices);

    if (!opened) {
        std::cerr << "[CameraSdkAdapter] Camera::Open() returned false serial="
                  << current_serial_number_ << std::endl;
        camera_.reset();
        is_x4_air_ = false;
        extended_capture_profile_ = ExtendedCaptureProfile::Unsupported;
        current_model_key_ = "unknown";
        current_capabilities_ = "status,capture,battery,storage,media,photo,video";
        current_serial_number_.clear();
        current_camera_name_.clear();
        current_firmware_version_.clear();
    } else {
        std::cout << "[CameraSdkAdapter] Camera::Open() succeeded serial="
                  << current_serial_number_
                  << " connected_after_open=" << (connected_after_open ? "true" : "false")
                  << std::endl;
    }

    return opened;
}

void CameraSdkAdapter::CloseCamera() {
    if (camera_) {
        camera_->Close();
        camera_.reset();
    }
    is_x4_air_ = false;
    extended_capture_profile_ = ExtendedCaptureProfile::Unsupported;
    current_model_key_ = "unknown";
    current_capabilities_ = "status,capture,battery,storage,media,photo,video";
    preview_active_ = false;
    last_mode_hint_ = "unknown";
    current_serial_number_.clear();
    current_camera_name_.clear();
    current_firmware_version_.clear();
}

bool CameraSdkAdapter::IsConnected() const {
    return camera_ && camera_->IsConnected();
}

bool CameraSdkAdapter::CaptureCurrentStatus() const {
    return camera_ && camera_->CaptureCurrentStatus();
}

const std::string& CameraSdkAdapter::CurrentSerialNumber() const {
    return current_serial_number_;
}

const std::string& CameraSdkAdapter::CurrentModelKey() const {
    return current_model_key_;
}

CameraInfo CameraSdkAdapter::CurrentCameraInfo() const {
    return CameraInfo{
        current_serial_number_,
        current_camera_name_.empty() ? "(active-session)" : current_camera_name_,
        current_firmware_version_.empty() ? "(active-session)" : current_firmware_version_,
        current_model_key_,
        current_capabilities_,
    };
}

bool CameraSdkAdapter::GetBatteryStatus(BatteryInfo* status) {
    if (!camera_ || status == nullptr) {
        return false;
    }

    ins_camera::BatteryStatus sdk_status{};
    if (!camera_->GetBatteryStatus(sdk_status)) {
        return false;
    }

    status->power_type = PowerTypeToString(sdk_status.power_type);
    status->battery_level = sdk_status.battery_level;
    status->battery_scale = sdk_status.battery_scale;
    return true;
}

bool CameraSdkAdapter::GetStorageState(StorageInfo* status) {
    if (!camera_ || status == nullptr) {
        return false;
    }

    ins_camera::StorageStatus sdk_status{};
    if (!camera_->GetStorageState(sdk_status)) {
        return false;
    }

    status->state = StorageStateToString(sdk_status.state);
    status->free_space = sdk_status.free_space;
    status->total_space = sdk_status.total_space;
    return true;
}

int64_t CameraSdkAdapter::GetCameraMediaTime() const {
    if (!camera_) {
        return -1;
    }
    return camera_->GetCameraMediaTime();
}

std::string CameraSdkAdapter::GetCameraLogFileUrl() const {
    if (!camera_) {
        return {};
    }
    return camera_->GetCameraLogFileUrl();
}

bool CameraSdkAdapter::SetNormalPhotoMode() {
    const bool ok = camera_ && camera_->SetPhotoSubMode(ins_camera::SubPhotoMode::PHOTO_SINGLE);
    if (ok) {
        last_mode_hint_ = "photo_single";
    }
    return ok;
}

bool CameraSdkAdapter::SetCaptureMode(const std::string& mode, std::string* error_message) {
    if (!camera_) {
        if (error_message != nullptr) {
            *error_message = "camera is not open";
        }
        return false;
    }

    ins_camera::SubPhotoMode photo_mode{};
    if (ParsePhotoMode(mode, &photo_mode)) {
        if (!camera_->SetPhotoSubMode(photo_mode)) {
            if (error_message != nullptr) {
                *error_message = "failed to switch photo mode: " + mode;
            }
            return false;
        }
        last_mode_hint_ = ToLowerCopy(mode);
        return true;
    }

    ins_camera::SubVideoMode video_mode{};
    if (ParseVideoMode(mode, &video_mode)) {
        if (!camera_->SetVideoSubMode(video_mode)) {
            if (error_message != nullptr) {
                *error_message = "failed to switch video mode: " + mode;
            }
            return false;
        }
        last_mode_hint_ = ToLowerCopy(mode);
        return true;
    }

    if (error_message != nullptr) {
        *error_message = "unsupported capture mode: " + mode;
    }
    return false;
}

bool CameraSdkAdapter::SetCaptureParameters(const std::map<std::string, std::string>& parameters,
                                            std::string* summary,
                                            std::string* error_message) {
    if (!camera_) {
        if (error_message != nullptr) {
            *error_message = "camera is not open";
        }
        return false;
    }

    auto exposure = std::make_shared<ins_camera::ExposureSettings>();
    auto capture = std::make_shared<ins_camera::CaptureSettings>();
    bool has_exposure = false;
    bool has_capture = false;
    bool has_global_sharpness = false;
    ins_camera::SharpnessLevel global_sharpness = ins_camera::SharpnessLevel::Medium;
    std::vector<std::string> applied_items;

    const auto mode_it = parameters.find("exposure_mode");
    if (mode_it != parameters.end()) {
        ins_camera::PhotographyOptions_ExposureMode mode{};
        if (!ParseExposureMode(mode_it->second, &mode)) {
            if (error_message != nullptr) {
                *error_message = "invalid exposure_mode: " + mode_it->second;
            }
            return false;
        }
        exposure->SetExposureMode(mode);
        has_exposure = true;
        applied_items.push_back("exposure_mode=" + mode_it->second);
    }

    const auto ev_it = parameters.find("ev");
    if (ev_it != parameters.end()) {
        int32_t ev = 0;
        if (!ParseInt(ev_it->second, &ev)) {
            if (error_message != nullptr) {
                *error_message = "invalid ev: " + ev_it->second;
            }
            return false;
        }
        exposure->SetEVBias(ev);
        has_exposure = true;
        applied_items.push_back("ev=" + ev_it->second);
    }

    const auto iso_it = parameters.find("iso");
    if (iso_it != parameters.end()) {
        int32_t iso = 0;
        if (!ParseInt(iso_it->second, &iso)) {
            if (error_message != nullptr) {
                *error_message = "invalid iso: " + iso_it->second;
            }
            return false;
        }
        exposure->SetIso(iso);
        has_exposure = true;
        applied_items.push_back("iso=" + iso_it->second);
    }

    const auto shutter_it = parameters.find("shutter");
    if (shutter_it != parameters.end()) {
        double shutter = 0.0;
        if (!ParseDouble(shutter_it->second, &shutter)) {
            if (error_message != nullptr) {
                *error_message = "invalid shutter: " + shutter_it->second;
            }
            return false;
        }
        exposure->SetShutterSpeed(shutter);
        has_exposure = true;
        applied_items.push_back("shutter=" + shutter_it->second);
    }

    const auto wb_it = parameters.find("wb");
    if (wb_it != parameters.end()) {
        ins_camera::PhotographyOptions_WhiteBalance wb{};
        if (!ParseWhiteBalance(wb_it->second, &wb)) {
            if (error_message != nullptr) {
                *error_message = "invalid wb: " + wb_it->second;
            }
            return false;
        }
        capture->SetWhiteBalance(wb);
        has_capture = true;
        applied_items.push_back("wb=" + wb_it->second);
    }

    const auto sharpness_it = parameters.find("sharpness");
    if (sharpness_it != parameters.end()) {
        if (!ParseGlobalSharpness(sharpness_it->second, &global_sharpness)) {
            if (error_message != nullptr) {
                *error_message = "invalid sharpness: " + sharpness_it->second;
            }
            return false;
        }
        has_global_sharpness = true;
        applied_items.push_back("sharpness=" + sharpness_it->second);
    }

    bool has_sensor = false;
    const auto sensor_it = parameters.find("lens");
    const auto sensor_alias_it = parameters.find("sensor");
    std::string sensor_value;
    if (sensor_it != parameters.end()) {
        sensor_value = sensor_it->second;
    } else if (sensor_alias_it != parameters.end()) {
        sensor_value = sensor_alias_it->second;
    }
    if (!sensor_value.empty()) {
        ins_camera::SensorDevice sensor{};
        if (!ParseSensorDevice(sensor_value, &sensor)) {
            if (error_message != nullptr) {
                *error_message = "invalid lens/sensor: " + sensor_value;
            }
            return false;
        }
        if (!camera_->SetActiveSensor(sensor)) {
            if (error_message != nullptr) {
                *error_message = "failed to apply active sensor";
            }
            return false;
        }
        has_sensor = true;
        applied_items.push_back("lens=" + sensor_value);
    }

    bool has_stitching = false;
    const auto stitching_it = parameters.find("stitching");
    if (stitching_it != parameters.end()) {
        bool enable_stitching = false;
        if (!ParseBool(stitching_it->second, &enable_stitching)) {
            if (error_message != nullptr) {
                *error_message = "invalid stitching: " + stitching_it->second;
            }
            return false;
        }
        if (!camera_->EnableInCameraStitching(enable_stitching)) {
            if (error_message != nullptr) {
                *error_message = "failed to apply in-camera stitching";
            }
            return false;
        }
        has_stitching = true;
        applied_items.push_back(std::string("stitching=") + (enable_stitching ? "on" : "off"));
    }

    bool has_timer = false;
    const auto timer_it = parameters.find("timer_sec");
    if (timer_it != parameters.end()) {
        int32_t timer_sec = 0;
        if (!ParseInt(timer_it->second, &timer_sec) || timer_sec < 0) {
            if (error_message != nullptr) {
                *error_message = "invalid timer_sec: " + timer_it->second;
            }
            return false;
        }
        if (!camera_->SetPhotoSubModeTimer(ins_camera::SubPhotoMode::PHOTO_SINGLE, timer_sec)) {
            if (error_message != nullptr) {
                *error_message = "failed to apply photo timer";
            }
            return false;
        }
        has_timer = true;
        applied_items.push_back("timer_sec=" + timer_it->second);
    }

    if (!has_exposure && !has_capture && !has_global_sharpness && !has_sensor && !has_stitching &&
        !has_timer) {
        if (error_message != nullptr) {
            *error_message =
                "set param requires at least one of exposure_mode/ev/iso/shutter/wb/sharpness/lens/stitching/timer_sec";
        }
        return false;
    }

    const ins_camera::CameraFunctionMode scope = ParseScope(parameters);
    if (has_exposure && !camera_->SetExposureSettings(scope, exposure)) {
        if (error_message != nullptr) {
            *error_message = "failed to apply exposure settings";
        }
        return false;
    }
    if (has_capture && !camera_->SetCaptureSettings(scope, capture)) {
        if (error_message != nullptr) {
            *error_message = "failed to apply capture settings";
        }
        return false;
    }
    if (has_global_sharpness && !camera_->SetGlobalSharpness(global_sharpness)) {
        if (error_message != nullptr) {
            *error_message = "failed to apply global sharpness";
        }
        return false;
    }

    if (summary != nullptr) {
        std::ostringstream oss;
        oss << "camera parameters applied scope="
            << (scope == ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_IMAGE ? "photo"
                                                                                     : "video");
        for (const auto& item : applied_items) {
            oss << " " << item;
        }
        *summary = oss.str();
    }
    return true;
}

bool CameraSdkAdapter::GetCaptureParameters(const std::map<std::string, std::string>& parameters,
                                            std::string* json_body,
                                            std::string* summary,
                                            std::string* error_message) const {
    if (!camera_) {
        if (error_message != nullptr) {
            *error_message = "camera is not open";
        }
        return false;
    }

    const ins_camera::CameraFunctionMode scope = ParseScope(parameters);
    const auto exposure = camera_->GetExposureSettings(scope);
    const auto capture = camera_->GetCaptureSettings(scope);
    ins_camera::SharpnessLevel sharpness = ins_camera::SharpnessLevel::Medium;
    const bool sharpness_ok = camera_->GetGlobalSharpness(sharpness);
    const auto lens_type = camera_->GetCameraLensType();

    if (!exposure || !capture) {
        if (error_message != nullptr) {
            *error_message = "failed to read camera parameters";
        }
        return false;
    }

    std::ostringstream oss;
    oss << "{\n"
        << "  \"scope\": \""
        << (scope == ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_IMAGE ? "photo"
                                                                                 : "video")
        << "\",\n"
        << "  \"last_mode_hint\": \"" << JsonEscape(last_mode_hint_) << "\",\n"
        << "  \"preview_active\": " << (preview_active_ ? "true" : "false") << ",\n"
        << "  \"lens_type\": \"" << LensTypeToString(lens_type) << "\",\n"
        << "  \"exposure_mode\": \"" << ExposureModeToString(exposure->ExposureMode()) << "\",\n"
        << "  \"ev\": " << exposure->EVBias() << ",\n"
        << "  \"iso\": " << exposure->Iso() << ",\n"
        << "  \"shutter\": " << exposure->ShutterSpeed() << ",\n"
        << "  \"wb\": \"" << WhiteBalanceToString(capture->WhiteBalance()) << "\"";
    if (sharpness_ok) {
        oss << ",\n  \"sharpness\": \"" << SharpnessLevelToString(sharpness) << "\"";
    }
    oss << "\n}";

    if (json_body != nullptr) {
        *json_body = oss.str();
    }
    if (summary != nullptr) {
        *summary = std::string("camera parameters queried scope=") +
                   (scope == ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_IMAGE ? "photo"
                                                                                        : "video");
    }
    return true;
}

CaptureResult CameraSdkAdapter::TakePhoto() {
    if (!camera_) {
        return {};
    }
    return ToCaptureResult(camera_->TakePhoto());
}

bool CameraSdkAdapter::TakePhotoAndDownload(const std::string& output_dir,
                                            std::vector<std::string>* local_paths) {
    if (output_dir.empty() || local_paths == nullptr || !SetNormalPhotoMode()) {
        return false;
    }

    const auto capture = TakePhoto();
    if (capture.origin_urls.empty()) {
        return false;
    }

    std::string normalized_dir = output_dir;
    if (!normalized_dir.empty() && normalized_dir.back() != '/') {
        normalized_dir.push_back('/');
    }

    for (const auto& remote_url : capture.origin_urls) {
        const std::string local_path = normalized_dir + FileNameFromUrl(remote_url);
        std::string error_message;
        if (!DownloadMedia(remote_url, local_path, timeout_ms_, &error_message)) {
            return false;
        }
        local_paths->push_back(local_path);
    }

    return true;
}

bool CameraSdkAdapter::SetNormalVideoMode() {
    const bool ok = camera_ && camera_->SetVideoSubMode(ins_camera::SubVideoMode::VIDEO_NORMAL);
    if (ok) {
        last_mode_hint_ = "video_normal";
    }
    return ok;
}

bool CameraSdkAdapter::StartRecording(const std::map<std::string, std::string>& parameters,
                                      std::string* summary,
                                      std::string* error_message) {
    if (!camera_) {
        if (error_message != nullptr) {
            *error_message = "camera is not open";
        }
        return false;
    }

    if (!SetNormalVideoMode()) {
        if (error_message != nullptr) {
            *error_message = "failed to switch to normal video mode";
        }
        return false;
    }

    bool has_record_params = false;
    ins_camera::RecordParams record_params;
    std::vector<std::string> applied_items;

    const auto resolution_it = parameters.find("resolution");
    if (resolution_it != parameters.end()) {
        if (!ParseNormalVideoResolution(resolution_it->second, &record_params.resolution)) {
            if (error_message != nullptr) {
                *error_message = "invalid video resolution: " + resolution_it->second;
            }
            return false;
        }
        has_record_params = true;
        applied_items.push_back("resolution=" +
                                NormalVideoResolutionToSummaryString(record_params.resolution));
    }

    const auto bitrate_it = parameters.find("bitrate");
    if (bitrate_it != parameters.end()) {
        int32_t bitrate = 0;
        if (!ParseInt(bitrate_it->second, &bitrate) || bitrate <= 0) {
            if (error_message != nullptr) {
                *error_message = "invalid bitrate: " + bitrate_it->second;
            }
            return false;
        }
        record_params.bitrate = bitrate;
        has_record_params = true;
        applied_items.push_back("bitrate=" + std::to_string(bitrate));
    }

    if (has_record_params &&
        !camera_->SetVideoCaptureParams(record_params,
                                        ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_VIDEO)) {
        if (error_message != nullptr) {
            *error_message = "failed to set normal video capture params";
        }
        return false;
    }

    if (!camera_->StartRecording()) {
        if (error_message != nullptr) {
            *error_message = "failed to start recording";
        }
        return false;
    }

    if (summary != nullptr) {
        if (applied_items.empty()) {
            *summary = "recording started";
        } else {
            std::ostringstream oss;
            oss << "recording started";
            for (const auto& item : applied_items) {
                oss << " " << item;
            }
            *summary = oss.str();
        }
    }
    return true;
}

CaptureResult CameraSdkAdapter::StopRecording() {
    if (!camera_) {
        return {};
    }
    return ToCaptureResult(camera_->StopRecording());
}

bool CameraSdkAdapter::StartExtendedCapture(const std::string& mode, std::string* error_message) {
    if (!camera_) {
        if (error_message != nullptr) {
            *error_message = "camera is not open";
        }
        return false;
    }

    if ((mode == "timeshift" || mode == "purevideo" || mode == "hdrvideo") &&
        extended_capture_profile_ == ExtendedCaptureProfile::Unsupported) {
        if (error_message != nullptr) {
            *error_message = mode + " is not enabled for camera model " + current_model_key_;
        }
        return false;
    }

    if (mode == "timeshift") {
        if (!camera_->SetVideoSubMode(ins_camera::SubVideoMode::VIDEO_TIMESHIFT)) {
            if (error_message != nullptr) {
                *error_message = "failed to switch to timeshift mode";
            }
            return false;
        }

        ins_camera::RecordParams record_params;
        record_params.resolution =
            is_x4_air_ ? ins_camera::VideoResolution::RES_6KP30
                       : ins_camera::VideoResolution::RES_57KP30;
        record_params.accelerate_fequency = 10;
        if (!camera_->SetVideoCaptureParams(record_params,
                                            ins_camera::CameraFunctionMode::FUNCTION_MODE_TIMESHIFT)) {
            if (error_message != nullptr) {
                *error_message = "failed to set timeshift capture params";
            }
            return false;
        }

        if (!camera_->StartRecording()) {
            if (error_message != nullptr) {
                *error_message = "failed to start timeshift capture";
            }
            return false;
        }
        return true;
    }

    if (mode == "purevideo") {
        if (is_x4_air_) {
            if (error_message != nullptr) {
                *error_message = "purevideo is not supported on X4 Air";
            }
            return false;
        }

        if (!camera_->SetVideoSubMode(ins_camera::SubVideoMode::VIDEO_PURE)) {
            if (error_message != nullptr) {
                *error_message = "failed to switch to purevideo mode";
            }
            return false;
        }

        ins_camera::RecordParams record_params;
        record_params.resolution = ins_camera::VideoResolution::RES_57KP30;
        if (!camera_->SetVideoCaptureParams(record_params,
                                            ins_camera::CameraFunctionMode::FUNCTION_MODE_PURE_VIDEO)) {
            if (error_message != nullptr) {
                *error_message = "failed to set purevideo capture params";
            }
            return false;
        }

        if (!camera_->StartRecording()) {
            if (error_message != nullptr) {
                *error_message = "failed to start purevideo capture";
            }
            return false;
        }
        return true;
    }

    if (mode == "hdrvideo") {
        if (!camera_->SetVideoSubMode(ins_camera::SubVideoMode::VIDEO_HDR)) {
            if (error_message != nullptr) {
                *error_message = "failed to switch to hdrvideo mode";
            }
            return false;
        }

        ins_camera::RecordParams record_params;
        record_params.resolution =
            is_x4_air_ ? ins_camera::VideoResolution::RES_6KP30
                       : ins_camera::VideoResolution::RES_57KP30;
        if (!camera_->SetVideoCaptureParams(record_params,
                                            ins_camera::CameraFunctionMode::FUNCTION_MODE_HDR_VIDEO)) {
            if (error_message != nullptr) {
                *error_message = "failed to set hdrvideo capture params";
            }
            return false;
        }

        if (!camera_->StartRecording()) {
            if (error_message != nullptr) {
                *error_message = "failed to start hdrvideo capture";
            }
            return false;
        }
        return true;
    }

    if (mode == "interval_photo") {
        const ins_camera::TimelapseParam param{
            ins_camera::CameraTimelapseMode::TIMELAPSE_INTERVAL_SHOOTING,
            0,
            3000,
            5,
        };
        if (!camera_->SetTimeLapseOption(param)) {
            if (error_message != nullptr) {
                *error_message = "failed to set interval photo options";
            }
            return false;
        }
        if (!camera_->StartTimeLapse(ins_camera::CameraTimelapseMode::TIMELAPSE_INTERVAL_SHOOTING)) {
            if (error_message != nullptr) {
                *error_message = "failed to start interval photo capture";
            }
            return false;
        }
        return true;
    }

    if (error_message != nullptr) {
        *error_message = "unsupported extended capture mode: " + mode;
    }
    return false;
}

CaptureResult CameraSdkAdapter::StopExtendedCapture(const std::string& mode,
                                                    std::string* error_message) {
    if (!camera_) {
        if (error_message != nullptr) {
            *error_message = "camera is not open";
        }
        return {};
    }

    if (mode == "timeshift" || mode == "purevideo" || mode == "hdrvideo") {
        return ToCaptureResult(camera_->StopRecording());
    }

    if (mode == "interval_photo") {
        return ToCaptureResult(
            camera_->StopTimeLapse(ins_camera::CameraTimelapseMode::TIMELAPSE_INTERVAL_SHOOTING));
    }

    if (error_message != nullptr) {
        *error_message = "unsupported extended capture mode: " + mode;
    }
    return {};
}

bool CameraSdkAdapter::GetRecordingFiles(std::vector<std::string>* file_list) {
    if (!camera_ || file_list == nullptr) {
        return false;
    }
    return camera_->GetRecordingFiles(*file_list);
}

std::vector<std::string> CameraSdkAdapter::ListMedia() {
    if (!camera_) {
        return {};
    }
    return camera_->GetCameraFilesList();
}

bool CameraSdkAdapter::StartPreview(const std::map<std::string, std::string>& parameters,
                                    std::string* summary,
                                    std::string* error_message) {
    if (!camera_) {
        if (error_message != nullptr) {
            *error_message = "camera is not open";
        }
        return false;
    }

    ins_camera::LiveStreamParam param;
    param.video_resolution = ins_camera::VideoResolution::RES_1920_1080P30;
    param.lrv_video_resulution = ins_camera::VideoResolution::RES_424_240P15;

    const auto resolution_it = parameters.find("resolution");
    if (resolution_it != parameters.end() &&
        !ParsePreviewResolution(resolution_it->second, &param.video_resolution)) {
        if (error_message != nullptr) {
            *error_message = "invalid preview resolution: " + resolution_it->second;
        }
        return false;
    }

    const auto audio_it = parameters.find("audio");
    if (audio_it != parameters.end() && !ParseBool(audio_it->second, &param.enable_audio)) {
        if (error_message != nullptr) {
            *error_message = "invalid audio: " + audio_it->second;
        }
        return false;
    }

    const auto video_it = parameters.find("video");
    if (video_it != parameters.end() && !ParseBool(video_it->second, &param.enable_video)) {
        if (error_message != nullptr) {
            *error_message = "invalid video: " + video_it->second;
        }
        return false;
    }

    const auto gyro_it = parameters.find("gyro");
    if (gyro_it != parameters.end() && !ParseBool(gyro_it->second, &param.enable_gyro)) {
        if (error_message != nullptr) {
            *error_message = "invalid gyro: " + gyro_it->second;
        }
        return false;
    }

    const auto lrv_it = parameters.find("using_lrv");
    if (lrv_it != parameters.end() && !ParseBool(lrv_it->second, &param.using_lrv)) {
        if (error_message != nullptr) {
            *error_message = "invalid using_lrv: " + lrv_it->second;
        }
        return false;
    }

    if (!camera_->StartLiveStreaming(param)) {
        if (error_message != nullptr) {
            *error_message = "failed to start preview stream";
        }
        return false;
    }

    preview_active_ = true;
    if (summary != nullptr) {
        *summary = "camera preview started";
    }
    return true;
}

bool CameraSdkAdapter::StopPreview(std::string* summary, std::string* error_message) {
    if (!camera_) {
        if (error_message != nullptr) {
            *error_message = "camera is not open";
        }
        return false;
    }
    if (!camera_->StopLiveStreaming()) {
        if (error_message != nullptr) {
            *error_message = "failed to stop preview stream";
        }
        return false;
    }

    preview_active_ = false;
    if (summary != nullptr) {
        *summary = "camera preview stopped";
    }
    return true;
}

bool CameraSdkAdapter::ShutdownForMaintenance(std::string* error_message) {
    if (!camera_) {
        if (error_message != nullptr) {
            *error_message = "camera is not open";
        }
        return false;
    }
    if (!camera_->ShutdownCamera()) {
        if (error_message != nullptr) {
            *error_message = "failed to shutdown camera";
        }
        return false;
    }

    preview_active_ = false;
    return true;
}

bool CameraSdkAdapter::GetMediaCount(int* count) {
    if (!camera_ || count == nullptr) {
        return false;
    }
    return camera_->GetCameraFilesCount(*count);
}

bool CameraSdkAdapter::DownloadMedia(const std::string& remote_path,
                                     const std::string& local_path,
                                     int timeout_ms,
                                     std::string* error_message) {
    if (!camera_ || remote_path.empty() || local_path.empty()) {
        if (error_message != nullptr) {
            *error_message = "camera/path is invalid";
        }
        return false;
    }

    ApplyTimeout(timeout_ms);
    int64_t last_percent = -1;
    const bool ok = camera_->DownloadCameraFile(
        remote_path,
        local_path,
        [&last_percent](int64_t downloaded, int64_t total) {
            if (total <= 0) {
                return;
            }
            const int64_t percent = downloaded * 100 / total;
            if (percent != last_percent) {
                last_percent = percent;
                std::cout << "\rdownload=" << percent << "%" << std::flush;
            }
        });
    std::cout << std::endl;
    ApplyTimeout(timeout_ms_);

    if (ok) {
        return true;
    }

    // BUG-A-004: DownloadCameraFile rejects .insv/.lrv files on Insta360 X5.
    // The camera also exposes an HTTP file server; try a direct HTTP GET as fallback.
    const std::string http_base = camera_->GetHttpBaseUrl();
    if (!http_base.empty()) {
        std::string http_error;
        std::cout << "download sdk_fail: trying http fallback base=" << http_base
                  << " path=" << remote_path << std::endl;
        if (HttpDownloadFile(http_base, remote_path, local_path, timeout_ms, &http_error)) {
            std::cout << "download http_fallback: ok" << std::endl;
            return true;
        }
        if (error_message != nullptr) {
            *error_message = "DownloadCameraFile returned false; http_fallback also failed: " +
                             http_error;
        }
    } else {
        if (error_message != nullptr) {
            *error_message = "DownloadCameraFile returned false; http_base_url empty (no fallback)";
        }
    }
    return false;
}

bool CameraSdkAdapter::DeleteMedia(const std::string& remote_path) {
    return camera_ && !remote_path.empty() && camera_->DeleteCameraFile(remote_path);
}

void CameraSdkAdapter::ApplyTimeout(int timeout_ms) {
    if (camera_ == nullptr || timeout_ms <= 0) {
        return;
    }
    camera_->SetTimeout(timeout_ms);
}

std::string CameraSdkAdapter::FileNameFromUrl(const std::string& url) {
    const auto end = url.find_first_of("?#");
    const auto path = url.substr(0, end);
    const auto slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

// BUG-A-004: Minimal HTTP/1.0 GET downloader using POSIX sockets.
// Used as fallback when DownloadCameraFile fails for video-type files.
// The Insta360 camera exposes an HTTP file server; this bypasses the SDK
// for paths that the SDK rejects but the HTTP server can serve.
bool CameraSdkAdapter::HttpDownloadFile(const std::string& http_base_url,
                                        const std::string& remote_path,
                                        const std::string& local_path,
                                        int timeout_ms,
                                        std::string* error_message) {
    // Build full URL and split into host:port + path
    static const std::string kPrefix = "http://";
    if (http_base_url.compare(0, kPrefix.size(), kPrefix) != 0) {
        if (error_message != nullptr) {
            *error_message = "http_fallback: base url must start with http://: " + http_base_url;
        }
        return false;
    }

    const std::string authority = http_base_url.substr(kPrefix.size());
    const auto auth_slash = authority.find('/');
    const std::string host_port = (auth_slash == std::string::npos)
                                      ? authority
                                      : authority.substr(0, auth_slash);

    std::string host;
    std::string port_str = "80";
    const auto colon_pos = host_port.rfind(':');
    if (colon_pos != std::string::npos) {
        host = host_port.substr(0, colon_pos);
        port_str = host_port.substr(colon_pos + 1);
    } else {
        host = host_port;
    }

    // URL-path is remote_path (already starts with '/')
    const std::string req_path = remote_path.empty() ? "/" : remote_path;

    // Resolve host
    struct addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0 || res == nullptr) {
        if (error_message != nullptr) {
            *error_message = "http_fallback: getaddrinfo failed for host=" + host;
        }
        return false;
    }

    const int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        freeaddrinfo(res);
        if (error_message != nullptr) {
            *error_message = "http_fallback: socket() failed";
        }
        return false;
    }

    // Apply timeout to send and receive
    if (timeout_ms > 0) {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        freeaddrinfo(res);
        close(sock);
        if (error_message != nullptr) {
            *error_message = "http_fallback: connect() failed to " + host + ":" + port_str;
        }
        return false;
    }
    freeaddrinfo(res);

    // Send HTTP/1.0 GET (no keep-alive, simpler EOF handling)
    const std::string request =
        "GET " + req_path + " HTTP/1.0\r\n"
        "Host: " + host + "\r\n"
        "Connection: close\r\n"
        "\r\n";
    if (send(sock, request.c_str(), request.size(), 0) < static_cast<ssize_t>(request.size())) {
        close(sock);
        if (error_message != nullptr) {
            *error_message = "http_fallback: send() failed";
        }
        return false;
    }

    // Read until end of HTTP headers (\r\n\r\n)
    std::string header_buf;
    header_buf.reserve(4096);
    char c = 0;
    bool header_done = false;
    while (header_buf.size() < 65536) {
        const ssize_t n = recv(sock, &c, 1, 0);
        if (n <= 0) {
            break;
        }
        header_buf.push_back(c);
        if (header_buf.size() >= 4 &&
            header_buf.compare(header_buf.size() - 4, 4, "\r\n\r\n") == 0) {
            header_done = true;
            break;
        }
    }

    if (!header_done) {
        close(sock);
        if (error_message != nullptr) {
            *error_message = "http_fallback: incomplete headers from " + http_base_url + req_path;
        }
        return false;
    }

    // Verify HTTP 200 status
    const auto first_crlf = header_buf.find("\r\n");
    const std::string status_line = first_crlf != std::string::npos
                                        ? header_buf.substr(0, first_crlf)
                                        : header_buf;
    if (status_line.find("200") == std::string::npos) {
        close(sock);
        if (error_message != nullptr) {
            *error_message = "http_fallback: server returned non-200: " + status_line +
                             " url=" + http_base_url + req_path;
        }
        return false;
    }

    // Stream response body to local file
    std::ofstream out(local_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        close(sock);
        if (error_message != nullptr) {
            *error_message = "http_fallback: cannot open local file for writing: " + local_path;
        }
        return false;
    }

    char buf[65536];
    while (true) {
        const ssize_t n = recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) {
            break;
        }
        out.write(buf, static_cast<std::streamsize>(n));
    }
    close(sock);
    out.close();

    if (!out) {
        if (error_message != nullptr) {
            *error_message = "http_fallback: write error on local file: " + local_path;
        }
        return false;
    }

    return true;
}

}  // namespace youyeetoo
