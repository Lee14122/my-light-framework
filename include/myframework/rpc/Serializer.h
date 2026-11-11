#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace myframework {

/**
 * RPC 方法参数/返回值抽象。
 *
 * - 默认用 JSON 字符串作为中转格式（方便调试）
 * - 未来可对接 Protobuf / FlatBuffers 实现零拷贝
 */
using RpcValue = std::string;

/**
 * 序列化器抽象接口。
 *
 * 可插拔设计：默认提供 JsonSerializer，用户可实现 ProtobufSerializer 替换。
 */
class Serializer {
public:
    virtual ~Serializer() = default;

    // 序列化 RpcValue → 网络传输字节流
    virtual std::string serialize(const RpcValue& val) = 0;

    // 反序列化 网络字节流 → RpcValue
    virtual RpcValue deserialize(const std::string& data) = 0;

    // ========== RPC 协议层面的便捷方法 ==========
    // 构建请求 payload
    virtual std::string buildRequest(const std::string& service,
                                     const std::string& method,
                                     const std::vector<RpcValue>& args) = 0;

    // 构建正常响应
    virtual std::string buildResponse(const RpcValue& result) = 0;

    // 构建错误响应
    virtual std::string buildError(int code, const std::string& message) = 0;

    // 从响应 payload 中提取结果或错误信息
    virtual std::pair<bool, RpcValue> parseResponse(const std::string& payload) = 0;
    // first=true → 正常响应, first=false → 错误 {code, msg}
};

/**
 * JSON 序列化器（默认实现，用于开发调试）。
 *
 * 请求格式: {"jsonrpc":"2.0","method":"Service.Method","params":[...],"id":seqId}
 * 响应格式: {"jsonrpc":"2.0","result":...}
 * 错误格式: {"jsonrpc":"2.0","error":{"code":-1,"message":"..."}}
 */
class JsonSerializer : public Serializer {
public:
    // 直接透传 JSON 字符串
    std::string serialize(const RpcValue& val) override;
    RpcValue deserialize(const std::string& data) override;

    std::string buildRequest(const std::string& service,
                             const std::string& method,
                             const std::vector<RpcValue>& args) override;
    std::string buildResponse(const RpcValue& result) override;
    std::string buildError(int code, const std::string& message) override;
    std::pair<bool, RpcValue> parseResponse(const std::string& payload) override;
};

/**
 * 二进制序列化器（可选实现，用于追求极致性能的场景）。
 *
 * 使用简单的 TLV 编码或对接 Protobuf。
 */
class BinarySerializer : public Serializer {
public:
    std::string serialize(const RpcValue& val) override;
    RpcValue deserialize(const std::string& data) override;

    std::string buildRequest(const std::string& service,
                             const std::string& method,
                             const std::vector<RpcValue>& args) override;
    std::string buildResponse(const RpcValue& result) override;
    std::string buildError(int code, const std::string& message) override;
    std::pair<bool, RpcValue> parseResponse(const std::string& payload) override;
};

} // namespace myframework