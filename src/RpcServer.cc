#include "myframework/rpc/RpcServer.h"
#include "myframework/Logger.h"

namespace myframework {

RpcServer::RpcServer(EventLoop* loop, const InetAddress& addr, const std::string& name)
    : loop_(loop)
    , serializer_(std::make_unique<JsonSerializer>())
{
    adapter_ = std::make_unique<TcpServerAdapter>(loop, addr, name);

    adapter_->setConnectionCallback(
        [this](const TcpConnectionPtr& conn) {
            this->onConnection(conn);
        });

    adapter_->setMessageCallback(
        [this](const TcpConnectionPtr& conn, Buffer* buf, Timestamp time) {
            this->onMessage(conn, buf, time);
        });
}

RpcServer::~RpcServer() = default;

void RpcServer::setThreadNum(int num) {
    adapter_->setThreadNum(num);
}

void RpcServer::start() {
    adapter_->start();
    MFLOG_INFO("RpcServer", "RPC Server started");
}

void RpcServer::stop() {
    MFLOG_INFO("RpcServer", "RPC Server stopping");
}

void RpcServer::registerMethod(const std::string& serviceName,
                                const std::string& methodName,
                                MethodHandler handler) {
    services_[serviceName][methodName] = std::move(handler);
    MFLOG_INFO("RpcServer", "Registered method: %s.%s",
               serviceName.c_str(), methodName.c_str());
}

void RpcServer::registerService(const std::string& serviceName,
                                 std::unordered_map<std::string, MethodHandler> methods) {
    for (auto& kv : methods) {
        registerMethod(serviceName, kv.first, std::move(kv.second));
    }
}

void RpcServer::setSerializer(std::unique_ptr<Serializer> s) {
    serializer_ = std::move(s);
}

void RpcServer::setHeartbeatInterval(int seconds) {
    heartbeatInterval_ = seconds;
}

void RpcServer::setIdleTimeout(int seconds) {
    idleTimeout_ = seconds;
}

// ========== 连接事件 ==========

void RpcServer::onConnection(const TcpConnectionPtr& conn) {
    if (conn->connected()) {
        auto codec = std::make_shared<RpcCodec>();
        codec->setSerializer(std::make_unique<JsonSerializer>());

        codec->setMessageCallback(
            [this, conn](const RpcMessage& msg) {
                this->onRpcMessage(conn, msg);
            });

        ConnInfo info;
        info.codec = codec;
        info.lastActiveTime = Timestamp::now();
        connections_[conn->name()] = std::move(info);

        MFLOG_INFO("RpcServer", "RPC connection established: %s",
                   conn->name().c_str());
    } else {
        connections_.erase(conn->name());
        MFLOG_INFO("RpcServer", "RPC connection closed: %s",
                   conn->name().c_str());
    }
}

// ========== 数据到达 ==========

void RpcServer::onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp time) {
    auto it = connections_.find(conn->name());
    if (it == connections_.end()) {
        MFLOG_WARN("RpcServer", "No codec for connection %s",
                   conn->name().c_str());
        buf->retrieveAll();
        return;
    }
    // 更新最后活跃时间（idle timeout 用）
    it->second.lastActiveTime = time;
    it->second.codec->onData(buf);
}

// ========== 收到完整 RPC 消息 ==========

void RpcServer::onRpcMessage(const TcpConnectionPtr& conn, const RpcMessage& msg) {
    switch (msg.type) {
        case RpcMessage::REQUEST:
            dispatch(conn, msg);
            break;
        case RpcMessage::HEARTBEAT:
            handleHeartbeat(conn, msg);
            break;
        default:
            break;
    }
}

// ========== 请求分发 ==========

void RpcServer::dispatch(const TcpConnectionPtr& conn, const RpcMessage& msg) {
    auto svcIt = services_.find(msg.serviceName);
    if (svcIt == services_.end()) {
        MFLOG_WARN("RpcServer", "Service not found: %s", msg.serviceName.c_str());
        std::string errPayload = serializer_->buildError(-1,
            "Service not found: " + msg.serviceName);
        sendResponse(conn, RpcCodec::makeError(msg.seqId, errPayload));
        return;
    }

    auto methodIt = svcIt->second.find(msg.methodName);
    if (methodIt == svcIt->second.end()) {
        MFLOG_WARN("RpcServer", "Method not found: %s.%s",
                   msg.serviceName.c_str(), msg.methodName.c_str());
        std::string errPayload = serializer_->buildError(-2,
            "Method not found: " + msg.methodName);
        sendResponse(conn, RpcCodec::makeError(msg.seqId, errPayload));
        return;
    }

    MFLOG_INFO("RpcServer", "Dispatching %s.%s (seq=%u)",
               msg.serviceName.c_str(), msg.methodName.c_str(), msg.seqId);

    // 构建上下文
    RpcContext ctx;
    ctx.seqId   = msg.seqId;
    ctx.service = msg.serviceName;
    ctx.method  = msg.methodName;
    ctx.meta    = msg.meta;

    // 用 Serializer 解析 payload → 参数数组
    auto parsed = serializer_->parseRequest(msg.payload);
    if (!parsed.ok) {
        MFLOG_WARN("RpcServer", "parseRequest failed: %s",
                   parsed.error.message.c_str());
        std::string errPayload = serializer_->buildError(-3, parsed.error.message);
        sendResponse(conn, RpcCodec::makeError(msg.seqId, errPayload));
        return;
    }
    ctx.args = RpcValue::array(std::move(parsed.value));  // [arg0, arg1, ...]

    uint32_t seqId = msg.seqId;
    auto connPtr  = conn;

    ctx.reply = [this, connPtr, seqId](const RpcValue& result) {
        std::string payload = serializer_->buildResponse(result);
        RpcMessage resp = RpcCodec::makeResponse(seqId, payload);
        sendResponse(connPtr, resp);
    };

    ctx.replyError = [this, connPtr, seqId](int code, const std::string& message) {
        std::string payload = serializer_->buildError(code, message);
        RpcMessage resp = RpcCodec::makeError(seqId, payload);
        sendResponse(connPtr, resp);
    };

    methodIt->second(ctx);
}

// ========== 发送响应 ==========

void RpcServer::sendResponse(const TcpConnectionPtr& conn, const RpcMessage& response) {
    std::string encoded = RpcCodec::encode(response);
    conn->send(encoded);
}

// ========== 心跳处理 ==========

void RpcServer::handleHeartbeat(const TcpConnectionPtr& conn, const RpcMessage& /*msg*/) {
    MFLOG_DEBUG("RpcServer", "Heartbeat from %s, replying", conn->name().c_str());
    RpcMessage pong = RpcCodec::makeHeartbeat();
    sendResponse(conn, pong);
}

// ========== 获取 EventLoop ==========

EventLoop* RpcServer::getLoop() const {
    return loop_;
}

} // namespace myframework