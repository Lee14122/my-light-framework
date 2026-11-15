#pragma once

#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <unordered_map>
#include <atomic>

#include "myframework/net/MuduoCompat.h"
#include "myframework/rpc/RpcMessage.h"
#include "myframework/rpc/RpcCodec.h"
#include "myframework/rpc/Serializer.h"

namespace myframework {

/**
 * RPC 客户端（基于 muduo::net::TcpClient + TimerQueue）。
 *
 * 功能:
 * - 连接池：支持多连接并发调用（每连接一个 TcpClient）
 * - 同步调用：syncCall() 阻塞等待结果
 * - 异步调用：asyncCall() 通过回调获取结果
 * - 超时控制：每个调用独立超时（muduo runAfter）
 * - 连接管理：TcpClient::enableRetry() 自动重连
 * - 心跳保活：muduo runEvery 定时发送
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
    // 连接到远端 RPC 服务器（1条连接）
    bool connect(const std::string& host, uint16_t port);

    // 建立连接池（N 条并发连接）
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

    // 同步调用（内部基于 asyncCall + promise/future）
    RpcValue syncCall(const std::string& service,
                      const std::string& method,
                      const RpcValue& args,
                      int timeoutMs = 5000);

    // ---- 心跳配置 ----
    void setHeartbeatInterval(int seconds);  // 0=禁用

    // ---- 连接回调 ----
    using ConnectedCallback = std::function<void()>;
    void setConnectedCallback(ConnectedCallback cb);

    // ---- 统计信息 ----
    int activeConnections() const;
    int pendingCalls() const;

private:
    // 一个 RPC 连接通道
    struct RpcChannel {
        std::unique_ptr<TcpClient> client;
        TcpConnectionPtr           conn;
        std::shared_ptr<RpcCodec>  codec;
        std::atomic_bool           ready;
        muduo::net::TimerId        heartbeatTimer;

        RpcChannel() : ready(false) {}
    };

    void onMessage(RpcChannel* channel, const RpcMessage& msg);
    RpcChannel* pickChannel();   // round-robin

    void doAsyncCall(const std::string& service,
                     const std::string& method,
                     const RpcValue& args,
                     SuccessCallback onSuccess,
                     ErrorCallback onError,
                     int timeoutMs);

    // 待处理的调用（仅在 loop 线程读写，无需锁）
    struct PendingCall {
        uint32_t           seqId;
        SuccessCallback    onSuccess;
        ErrorCallback      onError;
        muduo::net::TimerId timeoutTimer;
    };

    // 心跳
    void startHeartbeat(RpcChannel* channel);

    EventLoop* loop_;
    std::unique_ptr<Serializer> serializer_;
    std::vector<std::unique_ptr<RpcChannel>> channels_;
    size_t nextChannelIndex_ = 0;

    std::atomic_uint32_t nextSeqId_{1};

    // seqId → callback（仅在 loop 线程读写）
    std::unordered_map<uint32_t, std::unique_ptr<PendingCall>> pendingCalls_;

    ConnectedCallback connectedCallback_;
    int heartbeatInterval_ = 0;
};

} // namespace myframework