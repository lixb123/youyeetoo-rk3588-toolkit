#pragma once

#include <string>
#include <vector>

#include "camera_app.h"

namespace youyeetoo {

class UserDataStore {
public:
    virtual ~UserDataStore() = default;

    virtual std::string DefaultOutputDir(const CameraAppOptions& options) const = 0;
    virtual std::string LatestMediaListPath() const = 0;
    virtual std::string LatestMediaListJsonPath() const = 0;
    virtual std::string LatestParamQueryJsonPath() const = 0;
    virtual std::string LatestMediaTimeJsonPath() const = 0;
    virtual std::string LatestLogQueryJsonPath() const = 0;
    virtual std::string DeleteAllSummaryPath() const = 0;

    virtual bool EnsureDirectoryExists(const std::string& path, std::string* error_message) const = 0;
    virtual bool WriteTextFile(const std::string& path,
                               const std::string& content,
                               std::string* error_message) const = 0;
    virtual bool ReadLines(const std::string& path,
                           std::vector<std::string>* lines,
                           std::string* error_message) const = 0;
};

class FileSystemUserDataStore : public UserDataStore {
public:
    explicit FileSystemUserDataStore(const std::string& camera_namespace = std::string());

    static std::string StorageModeFlagPath();
    static bool UseNvmeStorageMode();
    static std::string CameraRootDir();
    static std::string RuntimeRootDir();
    static std::string BuildRuntimePath(const std::string& base_name,
                                        const std::string& extension,
                                        const std::string& camera_namespace);

    std::string DefaultOutputDir(const CameraAppOptions& options) const override;
    std::string LatestMediaListPath() const override;
    std::string LatestMediaListJsonPath() const override;
    std::string LatestParamQueryJsonPath() const override;
    std::string LatestMediaTimeJsonPath() const override;
    std::string LatestLogQueryJsonPath() const override;
    std::string DeleteAllSummaryPath() const override;
    std::string CameraDeviceListJsonPath() const;

    bool EnsureDirectoryExists(const std::string& path, std::string* error_message) const override;
    bool WriteTextFile(const std::string& path,
                       const std::string& content,
                       std::string* error_message) const override;
    bool ReadLines(const std::string& path,
                   std::vector<std::string>* lines,
                   std::string* error_message) const override;

private:
    std::string camera_namespace_;
};

}  // namespace youyeetoo
