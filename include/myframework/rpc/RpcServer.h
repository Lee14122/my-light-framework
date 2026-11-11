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
 *
 * 功能:
 * - 服务注册：serviceName → {methodName → handler}
 * - 请求分发：根据 service + method 找到对应 handler
 * - 心跳处理：自动响应客户端心跳
 * - 超时控制：支持设置调用超时
 *
 * 用法:
 *   RpcServer server(&loop, addr, "MyRpc");
 *   server.registerMethod("User", "Get", handler);
 *   server.start();
 */
class RpcServer {
public:
    using MethodHandler = std::function<void(RpcContext& ctx)>;

    RpcServer(EventLoop* loop, const InetAddress& addr, const std::string& name);
    ~RpcServer();

    void setThreadNum(int num);
    void start();
    void stop();

    // ---- 服务注册 ----
    // 注册单个方法
    void registerMethod(const std::string& serviceName,
                        const std::string& methodName,
                        MethodHandler handler);

    // 批量注册一个服务的所有方法
    void registerService(const std::string& serviceName,
                         std::unordered_map<std::string, MethodHandler> methods);

    // ---- 序列化 ----
    void setSerializer(std::unique_ptr<Serializer> s);

    // ---- 心跳配置 ----
    void setHeartbeatInterval(int seconds);   // 心跳间隔（秒），0=禁用
    void setIdleTimeout(int seconds);         // 心跳超时断开（秒）

private:
    void onConnection(const TcpConnectionPtr& conn);
    void onMessage(const TcpConnectionPtr& conn, Buffer* buf);
    void onRpcMessage(const TcpConnectionPtr& conn, const RpcMessage& msg);
    void sendResponse(const TcpConnectionPtr& conn, const RpcMessage& response);
    void handleHeartbeat(const TcpConnectionPtr& conn, const RpcMessage& msg);

    // 分发请求到注册的 handler
    void dispatch(const TcpConnectionPtr& conn, const RpcMessage& msg);

    std::unique_ptr<TcpServerAdapter> adapter_;
    std::unique_ptr<Serializer>        serializer_;

    // serviceName → (methodName → handler)
    std::unordered_map<std::string,
                       std::unordered_map<std::string, MethodHandler>> services_;

    // 心跳
    int heartbeatInterval_ = 0;   // 秒，0=禁用
    int idleTimeout_       = 60;  // 秒
};

} // namespace myframework