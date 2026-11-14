#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <memory>
#include <vector>

#include "EventLoop.h"
#include "InetAddress.h"
#include "myframework/rpc/RpcMessage.h"
#include "myframework/rpc/RpcCodec.h"
#include "myframework/rpc/Serializer.h"
#include "myframework/net/TcpServerAdapter.h"

namespace myframework {

/**
 * RPC 调用上下文：一次 RPC 调用的完整信息。
 */
struct RpcContext {
    uint32_t    seqId;           // 请求序列号
    std::string service;         // 服务名
    std::string method;          // 方法名
    RpcValue    args;            // 请求参数
    RpcValue    meta;            // 透传元数据

    // ---- 向客户端发送响应 ----
    // 正常响应
    std::function<void(const RpcValue&)> reply;
    // 错误响应
    std::function<void(int code, const std::string& message)> replyError;
};

/**
 * RPC 服务端。
 */
class RpcServer {
public:
    using MethodHandler = std::function<void(RpcContext& ctx)>;

    RpcServer(EventLoop* loop, const InetAddress& addr, const std::string& name);
    ~RpcServer();

    void setThreadNum(int num);
    void start();
    void stop();

    void registerMethod(const std::string& serviceName,
                        const std::string& methodName,
                        MethodHandler handler);

    void registerService(const std::string& serviceName,
                         std::unordered_map<std::string, MethodHandler> methods);

    void setSerializer(std::unique_ptr<Serializer> s);
    void setHeartbeatInterval(int seconds);
    void setIdleTimeout(int seconds);

    // 获取内部 EventLoop（供 Application 层使用）
    EventLoop* getLoop() const;

private:
    void onConnection(const TcpConnectionPtr& conn);
    void onMessage(const TcpConnectionPtr& conn, Buffer* buf);
    void onRpcMessage(const TcpConnectionPtr& conn, const RpcMessage& msg);
    void sendResponse(const TcpConnectionPtr& conn, const RpcMessage& response);
    void handleHeartbeat(const TcpConnectionPtr& conn, const RpcMessage& msg);
    void dispatch(const TcpConnectionPtr& conn, const RpcMessage& msg);

    EventLoop* loop_;  // 保存 EventLoop 引用
    std::unique_ptr<TcpServerAdapter> adapter_;
    std::unique_ptr<Serializer>        serializer_;

    // serviceName → (methodName → handler)
    std::unordered_map<std::string,
                       std::unordered_map<std::string, MethodHandler>> services_;

    // connection name → RpcCodec (per-connection codec)
    std::unordered_map<std::string, std::shared_ptr<RpcCodec>> codecs_;

    int heartbeatInterval_ = 0;
    int idleTimeout_       = 60;
};

} // namespace myframework