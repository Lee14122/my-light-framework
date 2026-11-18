#include "myframework/rpc/RpcValue.h"

#include <stdexcept>

namespace myframework {

// ============================================================================
// rpcErrorCodeString()
// ============================================================================

const char* rpcErrorCodeString(RpcErrorCode code) {
    switch (code) {
    case RpcErrorCode::OK:                      return "OK";
    case RpcErrorCode::SERVICE_NOT_FOUND:       return "Service not found";
    case RpcErrorCode::METHOD_NOT_FOUND:        return "Method not found";
    case RpcErrorCode::PARSE_ERROR:             return "Parse error";
    case RpcErrorCode::INVALID_ARGUMENT:        return "Invalid argument";
    case RpcErrorCode::TIMEOUT:                 return "Timeout";
    case RpcErrorCode::CONNECTION_LOST:         return "Connection lost";
    case RpcErrorCode::NO_AVAILABLE_CONNECTION: return "No available connection";
    case RpcErrorCode::UNKNOWN:                 return "Unknown error";
    default:                                    return "?";
    }
}

// ============================================================================
// 内部辅助: nlohmann::json 类型 → ValueType
// ============================================================================

static ValueType typeOf(const nlohmann::json& j) {
    switch (j.type()) {
    case nlohmann::json::value_t::null:            return ValueType::Null;
    case nlohmann::json::value_t::boolean:         return ValueType::Bool;
    case nlohmann::json::value_t::number_integer:  return ValueType::Int;
    case nlohmann::json::value_t::number_unsigned: return ValueType::Int;
    case nlohmann::json::value_t::number_float:    return ValueType::Double;
    case nlohmann::json::value_t::string:          return ValueType::String;
    case nlohmann::json::value_t::array:           return ValueType::Array;
    case nlohmann::json::value_t::object:          return ValueType::Object;
    case nlohmann::json::value_t::binary:          return ValueType::Array;  // fallback
    case nlohmann::json::value_t::discarded:       return ValueType::Null;
    default:                                       return ValueType::Null;
    }
}

// ============================================================================
// 递归计算 JSON 深度（用于 parse 时的深度限制）
// ============================================================================

static size_t calcDepth(const nlohmann::json& j) {
    if (!j.is_array() && !j.is_object()) return 0;
    size_t maxChild = 0;
    for (const auto& el : j) {
        // nlohmann::json 遍历 object 时 el 是 value 部分
        size_t d = calcDepth(el);
        if (d > maxChild) maxChild = d;
    }
    return maxChild + 1;
}

// ============================================================================
// 构造
// ============================================================================

RpcValue::RpcValue(std::vector<RpcValue> arr) {
    nlohmann::json j = nlohmann::json::array();
    for (auto& v : arr) {
        j.push_back(std::move(v.value_));
    }
    value_ = std::move(j);
}

RpcValue::RpcValue(std::initializer_list<
                   std::pair<const std::string, RpcValue>> init) {
    nlohmann::json j = nlohmann::json::object();
    for (auto& kv : init) {
        j[kv.first] = std::move(kv.second.value_);
    }
    value_ = std::move(j);
}

RpcValue RpcValue::array(std::vector<RpcValue> arr) {
    return RpcValue(std::move(arr));
}

// ============================================================================
// 类型查询
// ============================================================================

ValueType RpcValue::type() const {
    return typeOf(value_);
}

// ============================================================================
// 取值
// ============================================================================

bool RpcValue::getBool() const {
    // nlohmann json.get<bool>() 会对 number 做隐式转换 (0→false, !0→true)
    if (!value_.is_boolean() && !value_.is_number()) {
        throw std::runtime_error("RpcValue::getBool: value is not bool or number");
    }
    return value_.get<bool>();
}

int RpcValue::getInt() const {
    if (!value_.is_number()) {
        throw std::runtime_error("RpcValue::getInt: value is not a number");
    }
    return value_.get<int>();
}

int64_t RpcValue::getInt64() const {
    if (!value_.is_number()) {
        throw std::runtime_error("RpcValue::getInt64: value is not a number");
    }
    return value_.get<int64_t>();
}

double RpcValue::getDouble() const {
    if (!value_.is_number()) {
        throw std::runtime_error("RpcValue::getDouble: value is not a number");
    }
    return value_.get<double>();
}

std::string RpcValue::getString() const {
    if (!value_.is_string()) {
        throw std::runtime_error("RpcValue::getString: value is not a string");
    }
    return value_.get<std::string>();
}

// ============================================================================
// 元素访问 — Object（按值返回）
// ============================================================================

RpcValue RpcValue::operator[](const std::string& key) const {
    if (!value_.is_object()) {
        throw std::runtime_error(
            "RpcValue::operator[]: value is not an object");
    }
    auto it = value_.find(key);
    if (it == value_.end()) {
        throw std::runtime_error(
            "RpcValue::operator[]: key '" + key + "' not found");
    }
    // 拷贝子对象 → 新的 RpcValue
    return RpcValue(nlohmann::json(*it));
}

// ============================================================================
// 元素访问 — Array（按值返回）
// ============================================================================

RpcValue RpcValue::operator[](size_t index) const {
    if (!value_.is_array()) {
        throw std::runtime_error(
            "RpcValue::operator[]: value is not an array");
    }
    if (index >= value_.size()) {
        throw std::runtime_error(
            "RpcValue::operator[]: index " + std::to_string(index)
            + " out of range (size=" + std::to_string(value_.size()) + ")");
    }
    return RpcValue(nlohmann::json(value_[index]));
}

// ============================================================================
// Object 操作
// ============================================================================

bool RpcValue::contains(const std::string& key) const {
    return value_.is_object() && value_.contains(key);
}

std::vector<std::string> RpcValue::keys() const {
    std::vector<std::string> out;
    if (!value_.is_object()) return out;
    for (auto& el : value_.items()) {
        out.push_back(el.key());
    }
    return out;
}

void RpcValue::set(const std::string& key, RpcValue val) {
    if (!value_.is_object()) {
        throw std::runtime_error("RpcValue::set: value is not an object");
    }
    value_[key] = std::move(val.value_);
}

// ============================================================================
// Array 操作
// ============================================================================

size_t RpcValue::size() const {
    if (value_.is_array())  return value_.size();
    if (value_.is_object()) return value_.size();
    return 0;
}

void RpcValue::push_back(RpcValue val) {
    if (!value_.is_array()) {
        throw std::runtime_error("RpcValue::push_back: value is not an array");
    }
    value_.push_back(std::move(val.value_));
}

// ============================================================================
// 序列化
// ============================================================================

std::string RpcValue::dump() const {
    return value_.dump();
}

RpcValue RpcValue::parse(const std::string& json, int maxDepth) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(
            std::string("RpcValue::parse: invalid JSON - ") + e.what());
    }

    // 深度检查
    size_t depth = calcDepth(j);
    if (static_cast<int>(depth) > maxDepth) {
        throw std::runtime_error(
            "RpcValue::parse: JSON nesting depth " + std::to_string(depth)
            + " exceeds limit " + std::to_string(maxDepth));
    }

    return RpcValue(std::move(j));
}

// ============================================================================
// 比较
// ============================================================================

bool RpcValue::operator==(const RpcValue& other) const {
    return value_ == other.value_;
}

} // namespace myframework