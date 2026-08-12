#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace youyeetoo {

// ─── CanConfig ────────────────────────────────────────────────────────────────
// 全部 CAN 参数从配置文件加载，源码中不硬编码任何设备名、波特率、CAN ID。
// 配置文件格式见 configs/can_config.txt。
// ─────────────────────────────────────────────────────────────────────────────
struct CanConfig {
    // SocketCAN 接口名；波特率由 ip link / systemd-networkd 负责设置，不在此处控制
    std::string primary_interface   = "can0";
    std::string backup_interface    = "can0";

    // 接收指令帧 CAN ID（待整星 ICD 确认后更新）
    uint32_t rx_command_id          = 0x101;
    // 发送遥测帧 CAN ID
    uint32_t tx_telemetry_id        = 0x201;

    // 主路连续 RX 错误达到该值后切换到备路
    int rx_error_threshold          = 10;

    // 周期遥测发送间隔（毫秒）
    int telemetry_interval_ms       = 1000;

    // 指令映射配置文件路径
    std::string command_map_path    = "/opt/youyeetoo_app/configs/can_command_map.txt";

    // 从文件加载配置。成功返回 true；失败时填写 *error 并保留字段默认值。
    bool LoadFromFile(const std::string& path, std::string* error = nullptr);
};

// ─── CanFrame ─────────────────────────────────────────────────────────────────
// 与 linux/can.h struct can_frame 平行的应用层表示，避免上层直接依赖内核头文件。
struct CanFrame {
    uint32_t can_id = 0;
    uint8_t  dlc    = 0;
    uint8_t  data[8]{};
};

// ─── ParsedCanCommand ─────────────────────────────────────────────────────────
// 协议层解析结果；调用方只需检查 valid 再使用 command_name / extra_params。
struct ParsedCanCommand {
    bool        valid            = false;
    std::string command_name;    // 例如 "GET_STATUS"
    std::string extra_params;    // 例如 "confirm=DELETE_ALL"，可为空
    std::string error_description;
};

// ─── CanProtocol ─────────────────────────────────────────────────────────────
// 纯协议解析/序列化层：只负责字节 ↔ 命令名映射，不含业务逻辑。
//
// 当前实现为"预 ICD 占位协议"（待 TBD-002 确认后替换本层实现，调用方不变）：
//   RX: data[0] = command_byte → 查 command_map_ → command_name
//       data[1..dlc-1] = 可选 ASCII 参数后缀
//   TX: status_summary 前 8 字节打入 data
//
// 换 ICD 只需修改 ParseRxFrame() 和 BuildTelemetryFrame() 函数体。
// ─────────────────────────────────────────────────────────────────────────────
class CanProtocol {
public:
    // 从文件加载 command_byte → command_name 映射。
    // 格式见 configs/can_command_map.txt。
    bool LoadCommandMap(const std::string& path, std::string* error = nullptr);

    // 解析一帧入站 CAN 数据。
    ParsedCanCommand ParseRxFrame(const CanFrame& frame,
                                  uint32_t expected_rx_id) const;

    // 构造一帧出站遥测 CAN 数据。
    CanFrame BuildTelemetryFrame(uint32_t tx_id,
                                 const std::string& status_summary) const;

    int CommandMapSize() const { return static_cast<int>(command_map_.size()); }

private:
    // key: command byte   value: {command_name, extra_params}
    std::map<uint8_t, std::pair<std::string, std::string>> command_map_;
};

}  // namespace youyeetoo
