#include "task_manager.h"

namespace youyeetoo {

TaskManager::TaskManager(ICameraManager* camera_manager) : camera_manager_(camera_manager) {}

void TaskManager::Configure(const CameraAppOptions& options) {
    options_ = options;
    bootstrapped_ = false;
    completion_recorded_ = false;
    completed_task_count_ = 0;
    last_task_summary_.clear();
    last_error_.clear();
}

bool TaskManager::Bootstrap(std::string* error_message) {
    bootstrapped_ = true;

    if (camera_manager_ == nullptr) {
        last_error_ = "task manager has no camera manager";
        if (error_message != nullptr) {
            *error_message = last_error_;
        }
        return false;
    }

    last_task_summary_ = "service bootstrapped without startup task";
    return true;
}

void TaskManager::Tick() {
    if (camera_manager_ == nullptr) {
        return;
    }

    const CameraControllerSnapshot& snapshot = camera_manager_->snapshot();
    if (!completion_recorded_ && camera_manager_->IsTaskFinished()) {
        completion_recorded_ = true;
        ++completed_task_count_;

        if (!snapshot.result_summary.empty()) {
            last_task_summary_ = snapshot.result_summary;
        } else {
            last_task_summary_ = "task finished without summary";
        }

        if (!snapshot.error_message.empty()) {
            last_error_ = snapshot.error_message;
        }
    }
}

bool TaskManager::SubmitTask(const CameraTaskRequest& request, std::string* error_message) {
    if (camera_manager_ == nullptr) {
        last_error_ = "camera manager is null";
        if (error_message != nullptr) {
            *error_message = last_error_;
        }
        return false;
    }

    if (camera_manager_->HasActiveTask()) {
        last_error_ = "another task is already running";
        if (error_message != nullptr) {
            *error_message = last_error_;
        }
        return false;
    }

    if (!camera_manager_->StartTask(request)) {
        last_error_ = "camera task submission failed";
        if (error_message != nullptr) {
            *error_message = last_error_;
        }
        return false;
    }

    completion_recorded_ = false;
    last_error_.clear();
    last_task_summary_ = "task submitted";
    if (error_message != nullptr) {
        error_message->clear();
    }
    return true;
}

bool TaskManager::SubmitModuleTask(const ModuleTask& task, std::string* error_message) {
    if (task.category != CommandCategory::Camera) {
        last_error_ = "task manager only supports camera module tasks";
        if (error_message != nullptr) {
            *error_message = last_error_;
        }
        return false;
    }

    CameraTaskRequest camera_request;
    camera_request.repeat_count = task.repeat_count;
    camera_request.output_dir = task.output_dir;
    camera_request.remote_path = task.remote_path;
    camera_request.local_path = task.local_path;
    camera_request.parameters = task.parameters;
    const auto mode_it = task.parameters.find("mode");
    if (mode_it != task.parameters.end()) {
        camera_request.extended_mode = mode_it->second;
    }
    const auto time_from_it = task.parameters.find("time_from");
    if (time_from_it != task.parameters.end()) {
        camera_request.batch_time_from = time_from_it->second;
    }
    const auto time_to_it = task.parameters.find("time_to");
    if (time_to_it != task.parameters.end()) {
        camera_request.batch_time_to = time_to_it->second;
    }
    const auto camera_serial_it = task.parameters.find("camera_serial");
    if (camera_serial_it != task.parameters.end()) {
        camera_request.target_camera_serial = camera_serial_it->second;
    }
    camera_request.delete_confirmed = task.delete_confirmed;

    if (task.task_name == "camera.init") {
        camera_request.type = CameraTaskType::Initialize;
    } else if (task.task_name == "camera.get_status") {
        camera_request.type = CameraTaskType::GetStatus;
    } else if (task.task_name == "camera.get_capture_status") {
        camera_request.type = CameraTaskType::GetCaptureStatus;
    } else if (task.task_name == "camera.get_battery") {
        camera_request.type = CameraTaskType::GetBattery;
    } else if (task.task_name == "camera.get_storage") {
        camera_request.type = CameraTaskType::GetStorage;
    } else if (task.task_name == "camera.take_photo_download") {
        camera_request.type = CameraTaskType::TakePhotoDownload;
    } else if (task.task_name == "camera.set_mode") {
        camera_request.type = CameraTaskType::SetMode;
    } else if (task.task_name == "camera.set_param") {
        camera_request.type = CameraTaskType::SetParam;
    } else if (task.task_name == "camera.get_param") {
        camera_request.type = CameraTaskType::GetParam;
    } else if (task.task_name == "camera.get_media_time") {
        camera_request.type = CameraTaskType::GetMediaTime;
    } else if (task.task_name == "camera.get_log") {
        camera_request.type = CameraTaskType::GetLog;
    } else if (task.task_name == "camera.list_devices") {
        camera_request.type = CameraTaskType::ListDevices;
    } else if (task.task_name == "camera.preview.start") {
        camera_request.type = CameraTaskType::PreviewStart;
    } else if (task.task_name == "camera.preview.stop") {
        camera_request.type = CameraTaskType::PreviewStop;
    } else if (task.task_name == "camera.shutdown_maint") {
        camera_request.type = CameraTaskType::ShutdownMaint;
    } else if (task.task_name == "camera.start_recording") {
        camera_request.type = CameraTaskType::StartRecording;
    } else if (task.task_name == "camera.stop_recording") {
        camera_request.type = CameraTaskType::StopRecording;
    } else if (task.task_name == "camera.ext_capture.start") {
        camera_request.type = CameraTaskType::StartExtendedCapture;
    } else if (task.task_name == "camera.ext_capture.stop") {
        camera_request.type = CameraTaskType::StopExtendedCapture;
    } else if (task.task_name == "camera.list_media") {
        camera_request.type = CameraTaskType::ListMedia;
    } else if (task.task_name == "camera.download_media") {
        camera_request.type = CameraTaskType::DownloadMedia;
    } else if (task.task_name == "camera.batch_download") {
        camera_request.type = CameraTaskType::BatchDownload;
    } else if (task.task_name == "camera.delete_media") {
        camera_request.type = CameraTaskType::DeleteMedia;
    } else if (task.task_name == "camera.delete_all_media") {
        camera_request.type = CameraTaskType::DeleteAllMedia;
    } else {
        last_error_ = "unsupported camera module task: " + task.task_name;
        if (error_message != nullptr) {
            *error_message = last_error_;
        }
        return false;
    }

    return SubmitTask(camera_request, error_message);
}

void TaskManager::SetInterfaceStatus(const std::string& status) {
    last_task_summary_ = status;
}

bool TaskManager::HasBootstrapped() const {
    return bootstrapped_;
}

bool TaskManager::HasActiveTask() const {
    return camera_manager_ != nullptr && camera_manager_->HasActiveTask();
}

bool TaskManager::BootstrapTaskFinished() const {
    return camera_manager_ != nullptr && camera_manager_->IsTaskFinished();
}

int TaskManager::completed_task_count() const {
    return completed_task_count_;
}

const std::string& TaskManager::last_task_summary() const {
    return last_task_summary_;
}

const std::string& TaskManager::last_error() const {
    return last_error_;
}

}  // namespace youyeetoo
