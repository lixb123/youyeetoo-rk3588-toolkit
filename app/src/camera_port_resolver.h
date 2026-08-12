#pragma once

#include <map>
#include <string>

namespace youyeetoo {

// CameraPortResolver 负责将物理 USB 端口路径（如 "1-1"、"6-1.2"）
// 映射到相机序列号，从而实现"槽位名稳定映射"（CAM-003）。
//
// 数据来源：
//   配置文件（camera_port_map.txt）  →  slot_name → usb_port_path
//   sysfs 扫描                        →  usb_port_path → serial_number
//   两者合并                           →  slot_name → serial_number
//
// 槽位名约定：cam0 / cam1 / cam2 / cam3（可在配置中自定义）。
// 以 "cam" 开头的 target_camera_serial 将由 CameraManager 自动解析。
//
// 设计原则：
//   - 只在主进程路由层运行，不进入 SDK 调用层（对进程隔离友好）。
//   - 解析失败（配置缺失、端口无相机）时安静返回空 serial，不抛异常。
class CameraPortResolver {
public:
    // 扫描 /sys/bus/usb/devices/，返回 {usb_port_path → serial_number}。
    // 默认返回 idVendor == "2e1a"（Insta360）的设备；可通过
    // CAMERA_USB_VENDOR_ID 和可选的 CAMERA_USB_PRODUCT_ID（逗号分隔）过滤。
    // 端口路径示例："1-1"、"6-1.2"，即 sysfs 目录名去掉末尾斜杠。
    static std::map<std::string, std::string> ScanUsbSerials();

    // 加载配置文件，返回 {slot_name → usb_port_path}。
    // 配置格式：每行 "slot_name=usb_port_path"，# 开头为注释，空行忽略。
    // 出错时 error_message 非空，返回空 map。
    static std::map<std::string, std::string> LoadPortMap(const std::string& config_path,
                                                          std::string* error_message);

    // 综合 LoadPortMap + ScanUsbSerials，返回 {slot_name → serial_number}。
    // 某槽位对应端口无相机时，该槽位映射到空字符串（代表"未接入"）。
    // error_message 记录加载配置时的错误（sysfs 失败不阻断，只记录）。
    static std::map<std::string, std::string> Resolve(const std::string& config_path,
                                                      std::string* error_message);

    // 判断字符串是否形如槽位名（以 "cam" 开头，其余为数字）。
    // 用于 CameraManager 判断是否需要做解析。
    static bool IsSlotName(const std::string& name);

private:
    static std::string TrimStr(const std::string& s);
};

}  // namespace youyeetoo
