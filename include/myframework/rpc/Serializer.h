#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include "myframework/rpc/RpcValue.h"

namespace myframework {

/**
 * 序列化器抽象接口。
 *
 * 设计原则:
 *  - 帧头已有 serviceName / methodName / seqId，payload 不再重复携带
 *  - buildRequest 只序列化参数数组，buildResponse 只序列化返回值本身
 *  - 所有解析方法统一返回 Result<T>，错误码 + 消息直接可用
 *
 * 可插拔设计：默认提供 JsonSerializer，用户可实现 ProtobufSerializer 替换。
 */
class Serializer {
public:
    virtual ~Serializer() = default;

    // ---- 底层单值互转 ----
    virtual std::string serialize(const RpcValue& val)   = 0;
    virtual RpcValue   deserialize(const std::string& data) = 0;

    // ---- RPC 请求 ----
    // 构建请求 payload：只序列化参数数组 [arg0, arg1, ...]
    virtual std::string buildRequest(const std::string& service,
                                     const std::string& method,
                                     const std::vector<RpcValue>& args) = 0;

    // 解析请求 payload → 参数数组
    virtual Result<std::vector<RpcValue>> parseRequest(
        const std::string& payload) = 0;

    // ---- RPC 响应 ----
    virtual std::string buildResponse(const RpcValue& result) = 0;
    virtual std::string buildError(int code, const std::string& message) = 0;

    // 解析响应 payload → Result<RpcValue>
    virtual Result<RpcValue> parseResponse(const std::string& payload) = 0;
};

/**
 * JSON 序列化器（默认实现）。
 *
 * 线上格式:
 *   请求: [arg0, arg1, ...]          (纯 JSON 数组)
 *   正常响应: <result>                 (任意 JSON 值)
 *   错误响应: {"error":{"code":-1,"message":"..."}}
 */
class JsonSerializer : public Serializer {
public:
    std::string serialize(const RpcValue& val) override;
    RpcValue   deserialize(const std::string& data) override;

    std::string buildRequest(const std::string& service,
                             const std::string& method,
                             const std::vector<RpcValue>& args) override;
    Result<std::vector<RpcValue>> parseRequest(
        const std::string& payload) override;

    std::string       buildResponse(const RpcValue& result) override;
    std::string       buildError(int code, const std::string& message) override;
    Result<RpcValue>  parseResponse(const std::string& payload) override;
};

/**
 * 二进制序列化器 — 基于 MessagePack。
 *
 * 与 JsonSerializer 语义完全一致，只是编码从 JSON 文本换成 MessagePack 二进制。
 */
class BinarySerializer : public Serializer {
public:
    std::string serialize(const RpcValue& val) override;
    RpcValue   deserialize(const std::string& data) override;

    std::string buildRequest(const std::string& service,
                             const std::string& method,
                             const std::vector<RpcValue>& args) override;
    Result<std::vector<RpcValue>> parseRequest(
        const std::string& payload) override;

    std::string       buildResponse(const RpcValue& result) override;
    std::string       buildError(int code, const std::string& message) override;
    Result<RpcValue>  parseResponse(const std::string& payload) override;
};

} // namespace myframework