#pragma once

#include <memory>
#include <functional>
#include <vector>
#include "Buffer.h"
#include "myframework/rpc/RpcMessage.h"
#include "myframework/rpc/Serializer.h"

namespace myframework {

/**
 * RPC 编解码器（处理 TCP 粘包/拆包）。
 *
 * 职责:
 * 1. 从 TCP 字节流中切出完整帧（基于 totalLen 字段）
 * 2. 将帧头 + 可变长字段打包为 RpcMessage 结构
 * 3. 将 RpcMessage 编码为网络字节流
 *
 * 使用方式:
 *   每个 TcpConnection 持有一个 RpcCodec 实例，
 *   在 onMessage 回调中调用 codec->onData(buf)。
 *   收到完整消息时，通过回调通知上层。
 */
class RpcCodec {
public:
    // 收到完整帧时的回调
    using MessageCallback = std::function<void(const RpcMessage&)>;

    RpcCodec();
    ~RpcCodec();

    // 设置序列化器（默认使用 JsonSerializer）
    void setSerializer(std::unique_ptr<Serializer> s);

    // 喂入 TCP 字节数据，自动处理拆包
    void onData(Buffer* buf);

    // 设置收到完整消息时的回调
    void setMessageCallback(MessageCallback cb);

    // ---- 编码（RpcMessage → 网络字节流）----
    static std::string encode(const RpcMessage& msg);
    static bool decode(const char* data, size_t len, RpcMessage& msg);

    // 构造快捷方法
    static RpcMessage makeRequest(uint32_t seqId,
                                  const std::string& service,
                                  const std::string& method,
                                  const std::string& payload);
    static RpcMessage makeResponse(uint32_t seqId, const std::string& payload);
    static RpcMessage makeError(uint32_t seqId, const std::string& errorPayload);
    static RpcMessage makeHeartbeat();

private:
    std::string decodePayload(const RpcMessage& msg);

    std::unique_ptr<Serializer> serializer_;
    MessageCallback messageCallback_;

    // 内部缓冲区，累积未完整的数据
    std::vector<char> pendingBuffer_;
    size_t expectedLen_ = 0;    // 期望的帧长度（0=等待帧头）
};

} // namespace myframework