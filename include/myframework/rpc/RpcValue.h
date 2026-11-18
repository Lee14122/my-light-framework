#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <initializer_list>
#include <utility>

// ---------------------------------------------------------------------------
// nlohmann::json 只在这里出现一次 — 框架其余文件仅 include RpcValue.h
// ---------------------------------------------------------------------------
#include <nlohmann/json.hpp>

namespace myframework {

// ============================================================================
// 类型枚举
// ============================================================================

enum class ValueType : uint8_t {
    Null   = 0,
    Bool   = 1,
    Int    = 2,   // 整数（无小数部分）
    Double = 3,   // 浮点数
    String = 4,
    Array  = 5,
    Object = 6,
};

// ============================================================================
// 统一错误码
// ============================================================================

enum class RpcErrorCode : int {
    OK                      = 0,

    // ---- 服务端错误 ----
    SERVICE_NOT_FOUND       = -1,   // 服务未注册
    METHOD_NOT_FOUND        = -2,   // 方法未注册
    PARSE_ERROR             = -3,   // payload 解析失败 (非法 JSON / 深度超限 / 不是数组)
    INVALID_ARGUMENT        = -4,   // handler 内参数校验失败（类型不对、数量不够等）

    // ---- 客户端错误 ----
    TIMEOUT                 = -5,   // 调用超时
    CONNECTION_LOST         = -6,   // 连接断开（通道进入 DEAD 状态）
    NO_AVAILABLE_CONNECTION = -7,   // 连接池无可用通道（全 DEAD / CONNECTING）

    // ---- 通用 ----
    UNKNOWN                 = -99,  // 未知错误（兜底，如 handler 抛异常）
};

/// 获取错误码对应的简短描述文本
const char* rpcErrorCodeString(RpcErrorCode code);

// ============================================================================
// 错误信息
// ============================================================================

struct RpcError {
    int         code;
    std::string message;

    RpcError() : code(0) {}
    RpcError(int c, std::string msg) : code(c), message(std::move(msg)) {}
    RpcError(RpcErrorCode c, std::string msg)
        : code(static_cast<int>(c)), message(std::move(msg)) {}
};

// ============================================================================
// 通用结果类型（可用于同步调用返回值、反序列化返回值等）
// ============================================================================

template <typename T>
struct Result {
    bool    ok;
    T       value;
    RpcError error;

    static Result success(T v) {
        Result r;
        r.ok = true;
        r.value = std::move(v);
        return r;
    }

    static Result fail(int code, std::string msg) {
        Result r;
        r.ok = false;
        r.error = RpcError(code, std::move(msg));
        return r;
    }
};

// ============================================================================
// RpcValue — 统一的值类型，封装 nlohmann::json
// ============================================================================

/**
 * 所有 RPC 参数、返回值、元数据的统一载体。
 *
 * 用法示例:
 *   RpcValue v;                          // null
 *   RpcValue v = true;                   // bool
 *   RpcValue v = 42;                     // int
 *   RpcValue v = "hello";                // string
 *   RpcValue v{{"name", "Alice"}};       // object
 *   RpcValue v = RpcValue::array({1,2}); // array
 *
 *   v.isObject(); v["name"].getString(); v.dump();
 */
class RpcValue {
public:

    // ====================================================================
    // 构造
    // ====================================================================

    RpcValue()                     = default;
    RpcValue(std::nullptr_t)       {}                        // null
    RpcValue(bool v)               : value_(v) {}
    RpcValue(int v)                : value_(v) {}
    RpcValue(int64_t v)            : value_(v) {}
    RpcValue(uint64_t v)           : value_(v) {}
    RpcValue(double v)             : value_(v) {}
    RpcValue(const char* v)        : value_(v) {}
    RpcValue(std::string v)        : value_(std::move(v)) {}
    RpcValue(std::vector<RpcValue> arr);

    // 对象构造: RpcValue{{"key", val}, {"key2", val2}}
    RpcValue(std::initializer_list<
             std::pair<const std::string, RpcValue>> init);

    // ---- 拷贝/移动 ----
    RpcValue(const RpcValue&)            = default;
    RpcValue(RpcValue&&)                 = default;
    RpcValue& operator=(const RpcValue&) = default;
    RpcValue& operator=(RpcValue&&)      = default;
    ~RpcValue()                          = default;

    // ---- 工厂: 数组 ----
    static RpcValue array(std::vector<RpcValue> arr = {});

    // ====================================================================
    // 类型查询
    // ====================================================================

    ValueType type() const;

    bool isNull()   const { return type() == ValueType::Null; }
    bool isBool()   const { return type() == ValueType::Bool; }
    bool isInt()    const { return type() == ValueType::Int; }
    bool isDouble() const { return type() == ValueType::Double; }
    bool isNumber() const { return isInt() || isDouble(); }
    bool isString() const { return type() == ValueType::String; }
    bool isArray()  const { return type() == ValueType::Array; }
    bool isObject() const { return type() == ValueType::Object; }

    // ====================================================================
    // 取值（类型不匹配时抛 std::runtime_error）
    // ====================================================================

    bool        getBool()   const;
    int         getInt()    const;
    int64_t     getInt64()  const;
    double      getDouble() const;
    std::string getString() const;

    // 泛型取值（对接 nlohmann 的 get<T>，类型不匹配时抛 nlohmann::json::exception）
    template <typename T>
    T get() const { return value_.get<T>(); }

    // ====================================================================
    // 元素访问（按值返回，安全无 UB）
    // 用的是浅拷贝，避免原有对象销毁，手里出现悬垂引用，同时优化后的浅拷贝保证了性能。安全和性能兼顾
    // 同时又加了防呆机制，避免json未找到返回null
    // ====================================================================

    // 对象: v["key"]
    RpcValue operator[](const std::string& key) const;

    // 数组: v[0]
    RpcValue operator[](size_t index) const;

    // ====================================================================
    // Object 操作
    // ====================================================================

    bool contains(const std::string& key) const;
    std::vector<std::string> keys() const;
    void set(const std::string& key, RpcValue val);
    //这里必须显示调用修改，不然就和前面的[]重载会有冲突，修改的只是浅拷贝的副本，所以必须显式调用

    // ====================================================================
    // Array 操作
    // ====================================================================

    size_t size() const;
    void push_back(RpcValue val);

    // ====================================================================
    // 序列化
    // ====================================================================

    /// 序列化为 JSON 字符串
    std::string dump() const;

    /// 从 JSON 字符串反序列化（非法 JSON / 超深度抛 std::runtime_error）
    static RpcValue parse(const std::string& json, int maxDepth = 512);

    // ====================================================================
    // 比较
    // ====================================================================

    bool operator==(const RpcValue& other) const;
    bool operator!=(const RpcValue& other) const { return !(*this == other); }

private:
    // ---- 仅 Serializer 子类可以访问内部 nlohmann::json ----
    friend class Serializer;
    friend class JsonSerializer;
    friend class BinarySerializer;

    /// 直接接管 nlohmann::json（供 parse / Serializer 内部使用）
    explicit RpcValue(nlohmann::json&& j) : value_(std::move(j)) {}

    nlohmann::json value_;
};

} // namespace myframework