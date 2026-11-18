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
 * 连接池通道状态机。
 *
 * CONNECTING  TcpClient 正在连接中（初始状态 / 重连中）
 * IDLE        已连接，无未完成调用（首选调度目标）
 * BUSY        已连接，有未完成调用（仍可 pipeline 新调用）
 * DEAD        已断开，等待 TcpClient::enableRetry() 自动重连
 */
enum class ChannelState : uint8_t {
    CONNECTING = 0,
    IDLE       = 1,
    BUSY       = 2,
    DEAD       = 3,
};

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
                   const std::vector<RpcValue>& params,
                   SuccessCallback onSuccess,
                   ErrorCallback onError,
                   int timeoutMs = 5000);

    // 同步调用（内部基于 asyncCall + promise/future）
    RpcValue syncCall(const std::string& service,
                      const std::string& method,
                      const std::vector<RpcValue>& params,
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
    // 一个 RPC 连接通道（含状态机）
    struct RpcChannel {
        std::unique_ptr<TcpClient> client;
        TcpConnectionPtr           conn;
        std::shared_ptr<RpcCodec>  codec;
        std::atomic<ChannelState>  state;         // 连接状态机
        int                        pendingCount = 0;  // 未完成调用数（仅 loop 线程读写）
        muduo::net::TimerId        heartbeatTimer;

        RpcChannel() : state(ChannelState::CONNECTING) {}
    };

    void onMessage(RpcChannel* channel, const RpcMessage& msg);
    RpcChannel* pickChannel();   // round-robin

    void doAsyncCall(const std::string& service,
                     const std::string& method,
                     const std::vector<RpcValue>& params, 
                     SuccessCallback onSuccess,
                     ErrorCallback onError,
                     int timeoutMs);

    // 待处理的调用（仅在 loop 线程读写，无需锁）
    struct PendingCall {
        uint32_t           seqId;
        RpcChannel*        channel = nullptr;  // 所属通道（用于断连时精准清理）
        SuccessCallback    onSuccess;
        ErrorCallback      onError;
        muduo::net::TimerId timeoutTimer;
        bool               hasTimeout = false;
    };

    // 心跳
    void startHeartbeat(RpcChannel* channel);

    // 状态机辅助
    // 断连时精准清理：仅 fail 属于该通道的未完成调用
    void failPendingCallsOnChannel(RpcChannel* channel, int code, const std::string& msg);
    // 调用完成/超时后递减通道 pendingCount，归零时 BUSY → IDLE
    void decrementChannelPending(RpcChannel* channel);

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