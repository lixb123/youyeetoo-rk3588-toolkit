#include "camera_port_resolver.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace youyeetoo {
namespace {

namespace fs = std::filesystem;

constexpr const char* kDefaultVendorId = "2e1a";

// sysfs USB 设备根目录
constexpr const char* kUsbDevicesRoot = "/sys/bus/usb/devices";

// 读取单行文本文件，去掉行尾换行符，失败返回空字符串
std::string ReadSysfsLine(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    if (!std::getline(f, line)) {
        return {};
    }
    while (!line.empty() &&
           (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) {
        line.pop_back();
    }
    return line;
}

std::string EnvOrDefault(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return (value != nullptr && *value != '\0') ? value : fallback;
}

bool ProductAllowed(const std::string& product, const std::string& configured) {
    if (configured.empty()) {
        return true;
    }
    std::stringstream stream(configured);
    std::string item;
    while (std::getline(stream, item, ',')) {
        std::size_t begin = 0;
        while (begin < item.size() && std::isspace(static_cast<unsigned char>(item[begin])) != 0) {
            ++begin;
        }
        std::size_t end = item.size();
        while (end > begin && std::isspace(static_cast<unsigned char>(item[end - 1])) != 0) {
            --end;
        }
        item = item.substr(begin, end - begin);
        if (item == product) {
            return true;
        }
    }
    return false;
}

std::string ModelKeyFromProduct(const std::string& product) {
    std::string compact;
    for (const unsigned char ch : product) {
        if (std::isalnum(ch) != 0) {
            compact.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    if (compact.find("insta360x4air") != std::string::npos) return "Insta360X4Air";
    if (compact.find("insta360x5") != std::string::npos) return "Insta360X5";
    if (compact.find("insta360x4") != std::string::npos) return "Insta360X4";
    if (compact.find("insta360x3") != std::string::npos) return "Insta360X3";
    if (compact.find("insta360onex2") != std::string::npos) return "Insta360OneX2";
    if (compact.find("insta360oners") != std::string::npos) return "Insta360OneRS";
    if (compact.find("insta360oner") != std::string::npos) return "Insta360OneR";
    if (compact.find("insta360onex") != std::string::npos) return "Insta360OneX";
    return "Unknown";
}

}  // namespace

// ─── public static ───────────────────────────────────────────────────────────

std::string CameraPortResolver::TrimStr(const std::string& s) {
    std::size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin])) != 0) {
        ++begin;
    }
    std::size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
        --end;
    }
    return s.substr(begin, end - begin);
}

bool CameraPortResolver::IsSlotName(const std::string& name) {
    // 匹配 "cam" + 至少一位数字，如 cam0、cam1、cam10
    if (name.size() < 4) {
        return false;
    }
    if (name.compare(0, 3, "cam") != 0) {
        return false;
    }
    for (std::size_t i = 3; i < name.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(name[i])) == 0) {
            return false;
        }
    }
    return true;
}

std::map<std::string, std::string> CameraPortResolver::ScanUsbSerials() {
    std::map<std::string, std::string> result;

    std::error_code ec;
    if (!fs::is_directory(kUsbDevicesRoot, ec)) {
        return result;  // 非 Linux 环境或 sysfs 不可用，静默返回空
    }

    for (const auto& entry : fs::directory_iterator(kUsbDevicesRoot, ec)) {
        if (ec) {
            break;
        }
        const std::string port_path = entry.path().filename().string();

        // 跳过非实体设备目录（如 "usb1"、"1-0:1.0" 接口节点等）
        // 实体设备路径形如 "1-1"、"6-1.2"，含 '-' 且不含 ':'
        if (port_path.find('-') == std::string::npos) {
            continue;
        }
        if (port_path.find(':') != std::string::npos) {
            continue;
        }

        // 过滤 Insta360 vendor，产品 ID 可选且支持多个值，避免 X5-only 假设。
        const std::string vendor =
            ReadSysfsLine((entry.path() / "idVendor").string());
        if (vendor != EnvOrDefault("CAMERA_USB_VENDOR_ID", kDefaultVendorId)) {
            continue;
        }
        const std::string product =
            ReadSysfsLine((entry.path() / "idProduct").string());
        if (!ProductAllowed(product, EnvOrDefault("CAMERA_USB_PRODUCT_ID", ""))) {
            continue;
        }

        // 读取序列号
        const std::string serial =
            ReadSysfsLine((entry.path() / "serial").string());
        if (serial.empty()) {
            continue;
        }

        result[port_path] = serial;
    }

    return result;
}

std::vector<CameraInfo> CameraPortResolver::ScanUsbCameras() {
    std::vector<CameraInfo> result;
    std::error_code ec;
    if (!fs::is_directory(kUsbDevicesRoot, ec)) {
        return result;
    }

    for (const auto& entry : fs::directory_iterator(kUsbDevicesRoot, ec)) {
        if (ec) break;
        const std::string port_path = entry.path().filename().string();
        if (port_path.find('-') == std::string::npos ||
            port_path.find(':') != std::string::npos) {
            continue;
        }

        const std::string vendor = ReadSysfsLine((entry.path() / "idVendor").string());
        if (vendor != EnvOrDefault("CAMERA_USB_VENDOR_ID", kDefaultVendorId)) continue;
        const std::string product_id = ReadSysfsLine((entry.path() / "idProduct").string());
        if (!ProductAllowed(product_id, EnvOrDefault("CAMERA_USB_PRODUCT_ID", ""))) continue;

        const std::string serial = ReadSysfsLine((entry.path() / "serial").string());
        if (serial.empty()) continue;
        const std::string product = ReadSysfsLine((entry.path() / "product").string());
        result.push_back(CameraInfo{
            serial,
            product.empty() ? "Insta360 USB camera" : product,
            "",
            ModelKeyFromProduct(product),
            "status,capture,battery,storage,media,photo,video",
        });
    }
    return result;
}

std::map<std::string, std::string> CameraPortResolver::LoadPortMap(
    const std::string& config_path,
    std::string* error_message) {
    std::map<std::string, std::string> result;

    std::ifstream f(config_path);
    if (!f) {
        if (error_message != nullptr) {
            *error_message = "camera_port_map: cannot open config: " + config_path;
        }
        return result;
    }

    std::string line;
    int line_num = 0;
    while (std::getline(f, line)) {
        ++line_num;
        const std::string trimmed = TrimStr(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        const auto eq = trimmed.find('=');
        if (eq == std::string::npos) {
            // 格式错误行，跳过（不中断）
            continue;
        }
        const std::string slot = TrimStr(trimmed.substr(0, eq));
        const std::string port = TrimStr(trimmed.substr(eq + 1));
        if (slot.empty() || port.empty()) {
            continue;
        }
        result[slot] = port;
    }

    return result;
}

std::map<std::string, std::string> CameraPortResolver::Resolve(
    const std::string& config_path,
    std::string* error_message) {
    std::map<std::string, std::string> slot_to_serial;

    // 加载配置文件：slot → port_path
    const std::map<std::string, std::string> slot_to_port =
        LoadPortMap(config_path, error_message);

    if (slot_to_port.empty()) {
        return slot_to_serial;  // error_message 已由 LoadPortMap 填写
    }

    // 扫描 sysfs：port_path → serial
    const std::map<std::string, std::string> port_to_serial = ScanUsbSerials();

    // 合并
    for (const auto& [slot, port] : slot_to_port) {
        const auto it = port_to_serial.find(port);
        // 对应端口无相机时映射到空字符串（代表"未接入"）
        slot_to_serial[slot] = (it != port_to_serial.end()) ? it->second : std::string{};
    }

    return slot_to_serial;
}

}  // namespace youyeetoo
