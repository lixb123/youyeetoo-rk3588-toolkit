#include "user_data_store.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace youyeetoo {
namespace {

namespace fs = std::filesystem;

std::string SanitizeNamespace(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    std::string sanitized;
    sanitized.reserve(value.size());
    for (char ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            sanitized.push_back(ch);
        } else {
            sanitized.push_back('_');
        }
    }
    return sanitized;
}

std::string BuildNamespacedPath(const std::string& base_name,
                                const std::string& extension,
                                const std::string& camera_namespace,
                                const std::string& runtime_root) {
    std::string path = runtime_root + "/" + base_name;
    if (!camera_namespace.empty()) {
        path += "_" + camera_namespace;
    }
    path += extension;
    return path;
}

}  // namespace

FileSystemUserDataStore::FileSystemUserDataStore(const std::string& camera_namespace)
    : camera_namespace_(SanitizeNamespace(camera_namespace)) {}

std::string FileSystemUserDataStore::StorageModeFlagPath() {
    return "/run/youyeetoo/storage_mode";
}

bool FileSystemUserDataStore::UseNvmeStorageMode() {
    std::ifstream input(StorageModeFlagPath());
    if (!input) {
        return false;
    }

    std::string value;
    std::getline(input, value);
    return value == "nvme_ok";
}

std::string FileSystemUserDataStore::CameraRootDir() {
    return UseNvmeStorageMode() ? "/data/youyeetoo/camera"
                                : "/opt/youyeetoo_app/runtime_data/camera";
}

std::string FileSystemUserDataStore::RuntimeRootDir() {
    return UseNvmeStorageMode() ? "/data/youyeetoo/runtime"
                                : "/var/opt/youyeetoo/runtime";
}

std::string FileSystemUserDataStore::BuildRuntimePath(const std::string& base_name,
                                                      const std::string& extension,
                                                      const std::string& camera_namespace) {
    return BuildNamespacedPath(base_name, extension, SanitizeNamespace(camera_namespace), RuntimeRootDir());
}

std::string FileSystemUserDataStore::DefaultOutputDir(const CameraAppOptions& options) const {
    (void)options;
    const std::string base_dir = CameraRootDir();
    if (!camera_namespace_.empty()) {
        return (fs::path(base_dir) / camera_namespace_).string();
    }
    return base_dir;
}

std::string FileSystemUserDataStore::LatestMediaListPath() const {
    return BuildRuntimePath("camera_last_media_list", ".txt", camera_namespace_);
}

std::string FileSystemUserDataStore::LatestMediaListJsonPath() const {
    return BuildRuntimePath("camera_last_media_list", ".json", camera_namespace_);
}

std::string FileSystemUserDataStore::LatestParamQueryJsonPath() const {
    return BuildRuntimePath("camera_param_query", ".json", camera_namespace_);
}

std::string FileSystemUserDataStore::LatestMediaTimeJsonPath() const {
    return BuildRuntimePath("camera_media_time", ".json", camera_namespace_);
}

std::string FileSystemUserDataStore::LatestLogQueryJsonPath() const {
    return BuildRuntimePath("camera_log_query", ".json", camera_namespace_);
}

std::string FileSystemUserDataStore::DeleteAllSummaryPath() const {
    return BuildRuntimePath("camera_delete_all_summary", ".json", camera_namespace_);
}

std::string FileSystemUserDataStore::CameraDeviceListJsonPath() const {
    return RuntimeRootDir() + "/camera_device_list.json";
}

bool FileSystemUserDataStore::EnsureDirectoryExists(const std::string& path,
                                                    std::string* error_message) const {
    try {
        if (path.empty()) {
            if (error_message != nullptr) {
                *error_message = "output directory is empty";
            }
            return false;
        }
        fs::create_directories(path);
        return true;
    } catch (const std::exception& ex) {
        if (error_message != nullptr) {
            *error_message = ex.what();
        }
        return false;
    }
}

bool FileSystemUserDataStore::WriteTextFile(const std::string& path,
                                            const std::string& content,
                                            std::string* error_message) const {
    std::string ensure_error;
    const fs::path target_path(path);
    if (target_path.has_parent_path() &&
        !EnsureDirectoryExists(target_path.parent_path().string(), &ensure_error)) {
        if (error_message != nullptr) {
            *error_message = ensure_error;
        }
        return false;
    }

    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        if (error_message != nullptr) {
            *error_message = "failed to write file: " + path;
        }
        return false;
    }

    output << content;
    return true;
}

bool FileSystemUserDataStore::ReadLines(const std::string& path,
                                        std::vector<std::string>* lines,
                                        std::string* error_message) const {
    if (lines == nullptr) {
        if (error_message != nullptr) {
            *error_message = "output lines is null";
        }
        return false;
    }

    std::ifstream input(path);
    if (!input) {
        if (error_message != nullptr) {
            *error_message = "file not found: " + path;
        }
        return false;
    }

    lines->clear();
    std::string line;
    while (std::getline(input, line)) {
        lines->push_back(line);
    }
    return true;
}

}  // namespace youyeetoo
