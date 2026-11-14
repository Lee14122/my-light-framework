#include "myframework/rpc/Serializer.h"
#include <sstream>
#include <cstdio>

namespace myframework {

// ========== JsonSerializer ==========

// 简易 JSON 转义（处理 \ " 和控制字符）
static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

std::string JsonSerializer::serialize(const RpcValue& val) {
    // 直接透传 JSON 字符串（或包装为字符串）
    return val;
}

RpcValue JsonSerializer::deserialize(const std::string& data) {
    return data;
}

std::string JsonSerializer::buildRequest(const std::string& service,
                                          const std::string& method,
                                          const std::vector<RpcValue>& args) {
    std::string out;
    out.reserve(256);
    out = "{\"jsonrpc\":\"2.0\",\"method\":\"";
    out += jsonEscape(service);
    out += ".";
    out += jsonEscape(method);
    out += "\",\"params\":[";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) out += ",";
        out += args[i];  // args 本身已经是 JSON 字符串
    }
    out += "],\"id\":0}";
    return out;
}

std::string JsonSerializer::buildResponse(const RpcValue& result) {
    // result 应为 JSON 值字符串
    std::string out;
    out.reserve(result.size() + 32);
    out = "{\"jsonrpc\":\"2.0\",\"result\":";
    out += result;
    out += "}";
    return out;
}

std::string JsonSerializer::buildError(int code, const std::string& message) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%d", code);
    std::string out;
    out.reserve(message.size() + 80);
    out = "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":";
    out += buf;
    out += ",\"message\":\"";
    out += jsonEscape(message);
    out += "\"}}";
    return out;
}

std::pair<bool, RpcValue> JsonSerializer::parseResponse(const std::string& payload) {
    // 简易 JSON 解析：检查是否含 "error" 字段
    // 正式版本应使用完整的 JSON 解析器
    if (payload.find("\"error\"") != std::string::npos) {
        // 返回错误信息
        return {false, payload};
    }
    // 提取 result 字段（简单实现：返回整个 payload）
    return {true, payload};
}

// ========== BinarySerializer (stub) ==========

std::string BinarySerializer::serialize(const RpcValue& val) {
    return val;
}

RpcValue BinarySerializer::deserialize(const std::string& data) {
    return data;
}

std::string BinarySerializer::buildRequest(const std::string& service,
                                            const std::string& method,
                                            const std::vector<RpcValue>& args) {
    // 简易实现：用 | 分隔
    std::string out;
    out += service + "|" + method;
    for (const auto& a : args) {
        out += "|" + a;
    }
    return out;
}

std::string BinarySerializer::buildResponse(const RpcValue& result) {
    return result;
}

std::string BinarySerializer::buildError(int code, const std::string& message) {
    return "ERR:" + std::to_string(code) + ":" + message;
}

std::pair<bool, RpcValue> BinarySerializer::parseResponse(const std::string& payload) {
    if (payload.size() >= 4 && payload.substr(0, 4) == "ERR:") {
        return {false, payload};
    }
    return {true, payload};
}

} // namespace myframework