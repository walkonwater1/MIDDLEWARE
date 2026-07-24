/**
 * ws_common.h — WebSocket 信令协议共享定义
 *
 * 通用消息信封: {"event":"...", "session_id":"...", "timestamp":..., "data":{...}}
 *
 * 事件名称已脱敏处理，不包含公司业务语义。
 */

#ifndef WS_COMMON_H
#define WS_COMMON_H

#include <chrono>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>

// ============================================================================
// 事件名常量 (C→S: 客户端→服务器)
// ============================================================================
namespace WsEvent {

// --- 客户端→服务器 ---
constexpr const char* kSessionInit   = "session_init";    // 握手: 获取会话信息
constexpr const char* kStreamBegin   = "stream_begin";    // 通知开始推流
constexpr const char* kStreamState   = "stream_state";    // 流状态变更 (started/completed)
constexpr const char* kHeartbeat     = "heartbeat";       // 应用层心跳

// --- 服务器→客户端 ---
constexpr const char* kSessionReady     = "session_ready";     // 握手响应: 返回会话信息
constexpr const char* kChannelReady     = "channel_ready";     // 通道就绪
constexpr const char* kDataNotify       = "data_notify";       // 通知有新数据可拉取
constexpr const char* kDataCancel       = "data_cancel";       // 取消当前数据
constexpr const char* kDataComplete     = "data_complete";     // 数据完成
constexpr const char* kStreamInterrupt  = "stream_interrupt";  // 中断当前流
constexpr const char* kProcessResult    = "process_result";    // 处理结果
constexpr const char* kInferenceResult  = "inference_result";  // 推理结果
constexpr const char* kTipMessage       = "tip_message";       // 提示信息

}  // namespace WsEvent

// ============================================================================
// 配置默认值
// ============================================================================
namespace WsConfig {
constexpr const char* kDefaultUrl            = "ws://localhost:9002";
constexpr const char* kDefaultClientId       = "client_001";
constexpr const char* kDefaultChannel        = "data";
constexpr int         kDefaultHeartbeatSec   = 15;
constexpr int         kDefaultConnectTimeoutMs = 5000;
constexpr int         kDefaultIoTimeoutSec   = 5;
constexpr uint16_t    kDefaultPort           = 9002;
}  // namespace WsConfig

// ============================================================================
// 简易 JSON 构建器 (不依赖第三方库, 仅用于 Demo)
// ============================================================================
namespace JsonBuilder {

inline std::string escape_json(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;
        }
    }
    return out;
}

inline int64_t now_ms() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

/// 构建通用信封
/// json = {"event":"...","session_id":"...","timestamp":...,"data":{...data_fields...}}
inline std::string build_envelope(const std::string& event,
                                   const std::string& session_id,
                                   const std::string& data_fields = "") {
    std::ostringstream oss;
    oss << "{"
        << "\"event\":\"" << escape_json(event) << "\","
        << "\"session_id\":\"" << escape_json(session_id) << "\","
        << "\"timestamp\":" << now_ms();
    if (!data_fields.empty()) {
        oss << ",\"data\":{" << data_fields << "}";
    }
    oss << "}";
    return oss.str();
}

/// 构建带 data 对象的信封 (data 字段以 JSON 子对象提供)
inline std::string build_envelope_data(const std::string& event,
                                        const std::string& session_id,
                                        const std::string& data_json) {
    std::ostringstream oss;
    oss << "{"
        << "\"event\":\"" << escape_json(event) << "\","
        << "\"session_id\":\"" << escape_json(session_id) << "\","
        << "\"timestamp\":" << now_ms();
    if (!data_json.empty()) {
        oss << ",\"data\":" << data_json;
    }
    oss << "}";
    return oss.str();
}

/// 提取 JSON 中的 event 字段 (简易解析，不依赖完整 JSON 库)
inline std::string extract_event(const std::string& json) {
    const char* key = "\"event\":\"";
    size_t pos = json.find(key);
    if (pos == std::string::npos) return "";
    pos += strlen(key);
    size_t end = json.find('"', pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

/// 提取 JSON 中的 session_id 字段
inline std::string extract_session_id(const std::string& json) {
    const char* key = "\"session_id\":\"";
    size_t pos = json.find(key);
    if (pos == std::string::npos) return "";
    pos += strlen(key);
    size_t end = json.find('"', pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

/// 提取 JSON 中 data 子对象内某个字段的值
inline std::string extract_data_field(const std::string& json, const std::string& field) {
    std::string key = "\"" + field + "\":\"";
    size_t pos = json.find(key);
    if (pos == std::string::npos) {
        // 尝试数字值
        key = "\"" + field + "\":";
        pos = json.find(key);
        if (pos == std::string::npos) return "";
        pos += key.size();
        size_t end = json.find_first_of(",}", pos);
        return json.substr(pos, end - pos);
    }
    pos += key.size();
    size_t end = json.find('"', pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

/// 生成 session_id (简单的 UUID v4 风格)
inline std::string generate_session_id() {
    static uint64_t counter = 0;
    counter++;
    auto t = static_cast<uint64_t>(now_ms());
    std::ostringstream oss;
    oss << std::hex
        << ((t >> 32) & 0xFFFFFFFF) << "-"
        << (t & 0xFFFF) << "-"
        << (counter & 0xFFFF);
    return oss.str();
}

}  // namespace JsonBuilder

#endif  // WS_COMMON_H
