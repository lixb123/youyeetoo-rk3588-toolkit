#include "can_protocol.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

namespace youyeetoo {

// ─── 内部工具 ─────────────────────────────────────────────────────────────────

namespace {

std::string Trim(const std::string& s) {
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const size_t last  = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

bool ParseHexUint32(const std::string& s, uint32_t* out) {
    if (s.empty()) return false;
    try {
        const unsigned long v = std::stoul(s, nullptr, 0);
        *out = static_cast<uint32_t>(v);
        return true;
    } catch (...) {
        return false;
    }
}

std::string Hex8(uint8_t v) {
    const char* digits = "0123456789abcdef";
    std::string r = "0x";
    r += digits[(v >> 4) & 0xF];
    r += digits[v & 0xF];
    return r;
}

}  // namespace

// ─── CanConfig::LoadFromFile ──────────────────────────────────────────────────

bool CanConfig::LoadFromFile(const std::string& path, std::string* error) {
    std::ifstream f(path);
    if (!f.is_open()) {
        if (error) *error = "cannot open " + path + "; using defaults";
        return false;
    }

    std::string line;
    while (std::getline(f, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = Trim(line.substr(0, eq));
        const std::string val = Trim(line.substr(eq + 1));
        if (val.empty()) continue;

        if      (key == "primary_interface")     { primary_interface   = val; }
        else if (key == "backup_interface")      { backup_interface    = val; }
        else if (key == "rx_command_id")         { ParseHexUint32(val, &rx_command_id); }
        else if (key == "tx_telemetry_id")       { ParseHexUint32(val, &tx_telemetry_id); }
        else if (key == "rx_error_threshold")    { try { rx_error_threshold = std::stoi(val); } catch (...) {} }
        else if (key == "telemetry_interval_ms") { try { telemetry_interval_ms = std::stoi(val); } catch (...) {} }
        else if (key == "command_map_path")      { command_map_path = val; }
        // 未知字段静默跳过，便于后续扩展配置文件而不破坏旧版本
    }
    return true;
}

// ─── CanProtocol::LoadCommandMap ──────────────────────────────────────────────
// 文件格式（configs/can_command_map.txt）：
//   hex_byte | command_name | optional_params
// 注释行以 # 开头；optional_params 可缺省。

bool CanProtocol::LoadCommandMap(const std::string& path, std::string* error) {
    std::ifstream f(path);
    if (!f.is_open()) {
        if (error) *error = "cannot open command map " + path;
        return false;
    }

    command_map_.clear();
    std::string line;
    while (std::getline(f, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        std::string byte_str, cmd_name, extra;
        if (!std::getline(ss, byte_str, '|')) continue;
        if (!std::getline(ss, cmd_name, '|')) continue;
        std::getline(ss, extra);  // 可选；不存在时为空

        byte_str = Trim(byte_str);
        cmd_name = Trim(cmd_name);
        extra    = Trim(extra);

        if (cmd_name.empty()) continue;

        uint32_t byte_val = 0;
        if (!ParseHexUint32(byte_str, &byte_val) || byte_val > 0xFF) continue;

        command_map_[static_cast<uint8_t>(byte_val)] = {cmd_name, extra};
    }

    if (command_map_.empty()) {
        if (error) *error = "command map is empty: " + path;
        return false;
    }
    return true;
}

// ─── CanProtocol::ParseRxFrame ────────────────────────────────────────────────
//
// 【预 ICD 占位协议——待 TBD-002 整星 ICD 确认后替换此函数体】
//
// 当前协议约定：
//   data[0]       = command_byte → 查 command_map_ 得到 command_name
//   data[1..dlc-1] = 可选 ASCII 参数后缀（遇 0x00 截断）
//
// ICD 确认后：替换本函数体，调用方（can_worker）保持不变。

ParsedCanCommand CanProtocol::ParseRxFrame(const CanFrame& frame,
                                           uint32_t expected_rx_id) const {
    ParsedCanCommand result;

    if (frame.can_id != expected_rx_id) {
        // 非指令帧（可能是其他设备遥测），不报错，调用方根据 valid=false 静默跳过
        return result;
    }
    if (frame.dlc < 1) {
        result.error_description = "empty frame dlc=0";
        return result;
    }

    const uint8_t cmd_byte = frame.data[0];
    const auto it = command_map_.find(cmd_byte);
    if (it == command_map_.end()) {
        result.error_description = "unknown command byte " + Hex8(cmd_byte);
        return result;
    }

    result.valid        = true;
    result.command_name = it->second.first;
    result.extra_params = it->second.second;  // 配置文件中的默认参数（如 confirm=DELETE_ALL）

    // data[1..dlc-1] 如有可打印 ASCII，追加到 extra_params
    if (frame.dlc > 1) {
        std::string ascii_suffix;
        for (int i = 1; i < frame.dlc; ++i) {
            const char c = static_cast<char>(frame.data[i]);
            if (c == '\0') break;
            if (c >= 0x20 && c < 0x7F) ascii_suffix += c;
        }
        if (!ascii_suffix.empty()) {
            if (!result.extra_params.empty()) result.extra_params += ' ';
            result.extra_params += ascii_suffix;
        }
    }

    return result;
}

// ─── CanProtocol::BuildTelemetryFrame ─────────────────────────────────────────
//
// 【预 ICD 占位协议——待 TBD-002 整星 ICD 确认后替换此函数体】
//
// 当前实现：将 status_summary 的前 8 个字节打入 CAN data，便于地面调试。
// ICD 确认后替换为正式的二进制编码格式。

CanFrame CanProtocol::BuildTelemetryFrame(uint32_t tx_id,
                                          const std::string& status_summary) const {
    CanFrame f;
    f.can_id = tx_id;
    const size_t len = std::min(status_summary.size(), size_t{8});
    for (size_t i = 0; i < len; ++i) {
        f.data[i] = static_cast<uint8_t>(status_summary[i]);
    }
    f.dlc = static_cast<uint8_t>(len);
    return f;
}

}  // namespace youyeetoo
