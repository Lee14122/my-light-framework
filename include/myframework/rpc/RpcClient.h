#pragma once

#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <unordered_map>
#include <deque>
#include <atomic>

#include "EventLoop.h"
#include "Callbacks.h"
#include "InetAddress.h"
#include "myframework/rpc/RpcMessage.h"
#include "myframework/rpc/RpcCodec.h"
#include "myframework/rpc/Serializer.h"

namespace myframework {

/**
 * RPC 客户端（高性能设计）。
 *
 * 功能:
 * - 连接池：支持多连接并发调用
 * - 同步调用：syncCall() 阻塞等待结果
 * - 异步调用：asyncCall() 通过回调获取结果
 * - 超时控制：每个调用可设置独立超时
 * - 连接管理：自动重连、心跳保活
 *
 * 用法:
 *   RpcClient client(&loop);
 *   client.connect("127.0.0.1", 9090);
 *
 *   // 异步调用
 *   client.asyncCall("UserService", "GetUser", args,
 *       [](const RpcValue& result) { ... },
 *       [](int code, const string& msg) { ... });
 *
 *   // 同步调用
 *   RpcValue result = client.syncCall("UserService", "GetUser", args);
 */
class RpcClient {
public:
    using SuccessCallback = std::function<void(const RpcValue& result)>;
    using ErrorCallback   = std::function<void(int code, const std::string& message)>;

    RpcClient(EventLoop* loop);
    ~RpcClient();

    // ---- 连接管理 ----
    // 连接到远端 RPC 服务器
    bool connect(const std::string& host, uint16_t port);

    // 建立多个连接（连接池），提升并发吞吐
    bool connectPool(const std::string& host, uint16_t port, int numConnections);

    // 断开所有连接
    void disconnect();

    // ---- 序列化 ----
    void setSerializer(std::unique_ptr<Serializer> s);

    // ---- 远程调用 ----
    // 异步调用
    void asyncCall(const std::string& service,
                   const std::string& method,
                   const RpcValue& args,
                   SuccessCallback onSuccess,
                   ErrorCallback onError,
                   int timeoutMs = 5000);

    // 同步调用（内部基于 asyncCall + 条件变量）
    RpcValue syncCall(const std::string& service,
                      const std::string& method,
                      const RpcValue& args,
                      int timeoutMs = 5000);

    // ---- 心跳配置 ----
    void setHeartbeatInterval(int seconds);  // 0=禁用

    // ---- 统计信息 ----
    int activeConnections() const;
    int pendingCalls() const;

private:
    // 一个 RPC 连接通道
    struct RpcChannel {
        TcpConnectionPtr conn;
        RpcCodec codec;
        std::atomic_bool ready;

        RpcChannel() : ready(false) {}
    };

    void onMessage(RpcChannel* channel, const RpcMessage& msg);
    RpcChannel* pickChannel();   // 选择一个可用连接（round-robin）

    // 待处理的调用
    struct PendingCall {
        SuccessCallback onSuccess;
        ErrorCallback   onError;
    };

    EventLoop* loop_;
    std::unique_ptr<Serializer> serializer_;
    std::vector<std::unique_ptr<RpcChannel>> channels_;
    size_t nextChannelIndex_ = 0;

    std::atomic_uint32_t nextSeqId_{1};

    // seqId → callback
    std::unordered_map<uint32_t, PendingCall> pendingCalls_;
};

} // namespace myframework