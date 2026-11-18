#include "myframework/rpc/Serializer.h"
#include "myframework/Logger.h"

#include <cstdint>
#include <vector>
#include <stdexcept>

namespace myframework {

// ============================================================================
// JsonSerializer — 去掉 JSON-RPC 信封，payload 纯数据
// ============================================================================

// ---- 单值 ----
std::string JsonSerializer::serialize(const RpcValue& val) {
    return val.value_.dump();
}

RpcValue JsonSerializer::deserialize(const std::string& data) {
    try {
        return RpcValue(nlohmann::json::parse(data));
    } catch (const nlohmann::json::parse_error& e) {
        MFLOG_WARN("JsonSerializer", "deserialize failed: %s", e.what());
        return RpcValue();
    }
}

// ---- 请求: 只序列化参数数组 [arg0, arg1, ...] ----
std::string JsonSerializer::buildRequest(const std::string& /*service*/,
                                          const std::string& /*method*/,
                                          const std::vector<RpcValue>& args) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& arg : args) {
        arr.push_back(arg.value_);
    }
    return arr.dump();
}

Result<std::vector<RpcValue>> JsonSerializer::parseRequest(const std::string& payload) {
    try {
        // 必须走 RpcValue::parse，这样深度检查才会生效！
        RpcValue paramsArray = RpcValue::parse(payload); 
        
        if (!paramsArray.isArray()) {
            return Result<std::vector<RpcValue>>::fail(static_cast<int>(RpcErrorCode::PARSE_ERROR), "Payload is not an array");
        }

        std::vector<RpcValue> params;
        params.reserve(paramsArray.size());
        for (size_t i = 0; i < paramsArray.size(); ++i) {
            params.push_back(paramsArray[i]); // 用你封装的 operator[] 取出来
        }
        return Result<std::vector<RpcValue>>::success(std::move(params));
        
    } catch (const std::exception& e) {
        return Result<std::vector<RpcValue>>::fail(static_cast<int>(RpcErrorCode::PARSE_ERROR), std::string("Parse error: ") + e.what());
    }
}

// ---- 响应: 返回值本身（无信封）----
std::string JsonSerializer::buildResponse(const RpcValue& result) {
    return result.value_.dump();
}

// ---- 错误: 统一 {"error":{"code":...,"message":...}} ----
std::string JsonSerializer::buildError(int code, const std::string& message) {
    nlohmann::json err;
    err["error"]["code"]    = code;
    err["error"]["message"] = message;
    return err.dump();
}

Result<RpcValue> JsonSerializer::parseResponse(const std::string& payload) {
    try {
        // 同样走安全大门
        RpcValue resp = RpcValue::parse(payload); 

        if (resp.isObject() && resp.contains("error")) {
            int code = static_cast<int>(RpcErrorCode::PARSE_ERROR);
            if (resp["error"].contains("code")) code = resp["error"]["code"].getInt();
            std::string msg = "Unknown error";
            if (resp["error"].contains("message")) msg = resp["error"]["message"].getString();
            return Result<RpcValue>::fail(code, msg); 
        }

        return Result<RpcValue>::success(resp);
    } catch (const std::exception& e) {
        return Result<RpcValue>::fail(static_cast<int>(RpcErrorCode::PARSE_ERROR), std::string("Parse error: ") + e.what());
    }
}

// ============================================================================
// BinarySerializer — MessagePack，与 JsonSerializer 语义完全一致
// ============================================================================

namespace {

std::string vecToString(std::vector<uint8_t>&& v) {
    return std::string(reinterpret_cast<const char*>(v.data()), v.size());
}

std::vector<uint8_t> stringToVec(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

} // namespace

// ---- 单值 ----
std::string BinarySerializer::serialize(const RpcValue& val) {
    try {
        return vecToString(nlohmann::json::to_msgpack(val.value_));
    } catch (const std::exception& e) {
        MFLOG_WARN("BinarySerializer", "serialize failed: %s", e.what());
        return {};
    }
}

RpcValue BinarySerializer::deserialize(const std::string& data) {
    try {
        return RpcValue(nlohmann::json::from_msgpack(stringToVec(data)));
    } catch (const std::exception& e) {
        MFLOG_WARN("BinarySerializer", "deserialize failed: %s", e.what());
        return RpcValue();
    }
}

// ---- 请求: 参数数组 → msgpack ----
std::string BinarySerializer::buildRequest(const std::string& /*service*/,
                                            const std::string& /*method*/,
                                            const std::vector<RpcValue>& args) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& arg : args) {
        arr.push_back(arg.value_);
    }
    try {
        return vecToString(nlohmann::json::to_msgpack(arr));
    } catch (const std::exception& e) {
        MFLOG_WARN("BinarySerializer", "buildRequest failed: %s", e.what());
        return {};
    }
}

Result<std::vector<RpcValue>> BinarySerializer::parseRequest(const std::string& payload) {
    nlohmann::json j;
    try {
        j = nlohmann::json::from_msgpack(stringToVec(payload));
    } catch (const std::exception& e) {
        return Result<std::vector<RpcValue>>::fail(static_cast<int>(RpcErrorCode::PARSE_ERROR),
            std::string("Parse error: ") + e.what());
    }

    if (!j.is_array()) {
        return Result<std::vector<RpcValue>>::fail(static_cast<int>(RpcErrorCode::PARSE_ERROR), "Payload is not an array");
    }

    std::vector<RpcValue> params;
    params.reserve(j.size());
    for (const auto& item : j) {
        params.push_back(RpcValue(nlohmann::json(item)));
    }
    return Result<std::vector<RpcValue>>::success(std::move(params));
}

// ---- 响应: 返回值本身 → msgpack ----
std::string BinarySerializer::buildResponse(const RpcValue& result) {
    try {
        return vecToString(nlohmann::json::to_msgpack(result.value_));
    } catch (const std::exception& e) {
        MFLOG_WARN("BinarySerializer", "buildResponse failed: %s", e.what());
        return {};
    }
}

// ---- 错误: 同 buildError 格式 → msgpack ----
std::string BinarySerializer::buildError(int code, const std::string& message) {
    nlohmann::json err;
    err["error"]["code"]    = code;
    err["error"]["message"] = message;
    try {
        return vecToString(nlohmann::json::to_msgpack(err));
    } catch (const std::exception& e) {
        MFLOG_WARN("BinarySerializer", "buildError failed: %s", e.what());
        return {};
    }
}

Result<RpcValue> BinarySerializer::parseResponse(const std::string& payload) {
    nlohmann::json j;
    try {
        j = nlohmann::json::from_msgpack(stringToVec(payload));
    } catch (const std::exception& e) {
        return Result<RpcValue>::fail(static_cast<int>(RpcErrorCode::PARSE_ERROR),
            std::string(buildError(static_cast<int>(RpcErrorCode::PARSE_ERROR), std::string("Parse error: ") + e.what())));
    }

     if (j.contains("error") && j["error"].is_object()) {
        // 真正提取服务端传来的 code 和 message
        int code = static_cast<int>(RpcErrorCode::PARSE_ERROR);
        if (j["error"].contains("code")) code = j["error"]["code"].get<int>();
        
        std::string msg = "Unknown error";
        if (j["error"].contains("message")) msg = j["error"]["message"].get<std::string>();
        
        return Result<RpcValue>::fail(code, msg); 
    }


    return Result<RpcValue>::success(RpcValue(nlohmann::json(j)));
}

} // namespace myframework