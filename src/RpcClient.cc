#include "myframework/rpc/RpcClient.h"
#include "myframework/Logger.h"
#include "Channel.h"
#include "Socket.h"
#include "InetAddress.h"
#include "TcpConnection.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <mutex>
#include <condition_variable>
#include <future>

namespace myframework {

RpcClient::RpcClient(EventLoop* loop)
    : loop_(loop)
    , serializer_(std::make_unique<JsonSerializer>())
{
}

RpcClient::~RpcClient() {
    disconnect();
}

void RpcClient::setSerializer(std::unique_ptr<Serializer> s) {
    serializer_ = std::move(s);
}

// ========== 连接建立（非阻塞 connect） ==========

// 每个正在连接的上下文
struct ConnectContext {
    RpcClient* client;
    int sockfd;
    InetAddress serverAddr;
    std::unique_ptr<Channel> channel;
};

void RpcClient::onConnectWrite(ConnectContext* ctx) {
    // EPOLLOUT 触发，检查连接是否成功
    int err = 0;
    socklen_t len = sizeof(err);
    if (::getsockopt(ctx->sockfd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
        MFLOG_ERROR("RpcClient", "Connect failed: %s", strerror(err));
        ::close(ctx->sockfd);
        delete ctx;
        return;
    }

    // 先将旧 Channel 从 epoll 中移除（避免与 TcpConnection 的 Channel 冲突）
    ctx->channel->disableAll();
    ctx->channel->remove();

    // 获取本地地址
    struct sockaddr_in localAddr;
    socklen_t addrLen = sizeof(localAddr);
    ::getsockname(ctx->sockfd, (struct sockaddr*)&localAddr, &addrLen);
    InetAddress localInetAddr(localAddr);

    // 创建 RpcChannel
    auto channel = std::make_unique<RpcChannel>();
    channel->ready = false;

    // 创建 TcpConnection
    static std::atomic_int connIdx{0};
    int idx = connIdx.fetch_add(1);
    char nameBuf[128];
    snprintf(nameBuf, sizeof(nameBuf), "rpc-client-%d", idx);

    TcpConnectionPtr conn(new TcpConnection(
        ctx->client->loop_,
        nameBuf,
        ctx->sockfd,
        localInetAddr,
        ctx->serverAddr
    ));

    // 设置 RpcCodec
    auto codec = std::make_shared<RpcCodec>();
    codec->setSerializer(std::make_unique<JsonSerializer>());

    RpcChannel* rawChannel = channel.get();
    RpcClient*  clientPtr  = ctx->client;

    codec->setMessageCallback(
        [clientPtr, rawChannel](const RpcMessage& msg) {
            clientPtr->onMessage(rawChannel, msg);
        });

    channel->conn  = conn;
    channel->codec = codec;

    // 设置 TcpConnection 回调
    conn->setConnectionCallback(
        [clientPtr, rawChannel](const TcpConnectionPtr& c) {
            if (c->connected()) {
                rawChannel->ready = true;
                MFLOG_INFO("RpcClient", "Client channel connected: %s", c->name().c_str());
                if (clientPtr->connectedCallback_) {
                    clientPtr->connectedCallback_();
                }
            } else {
                rawChannel->ready = false;
                MFLOG_INFO("RpcClient", "Client channel disconnected: %s", c->name().c_str());
            }
        });

    conn->setMessageCallback(
        [codec](const TcpConnectionPtr&, Buffer* buf, Timestamp) {
            codec->onData(buf);
        });

    conn->setCloseCallback([](const TcpConnectionPtr& c) {
        MFLOG_INFO("RpcClient", "Client channel closed: %s", c->name().c_str());
    });

    // 先加入 channels_，再调 connectEstablished（回调中需要 channel 已就绪）
    ctx->client->channels_.push_back(std::move(channel));

    // 必须在 loop 线程中调用 connectEstablished
    ctx->client->loop_->runInLoop([conn]() {
        conn->connectEstablished();
    });

    delete ctx;
}

// ========== connect / connectPool ==========

bool RpcClient::connect(const std::string& host, uint16_t port) {
    return connectPool(host, port, 1);
}

bool RpcClient::connectPool(const std::string& host, uint16_t port, int numConnections) {
    // InetAddress 构造: (port, ip)
    InetAddress serverAddr(port, host);

    for (int i = 0; i < numConnections; ++i) {
        int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (sockfd < 0) {
            MFLOG_ERROR("RpcClient", "socket() failed: %s", strerror(errno));
            return false;
        }

        // 发起非阻塞 connect
        const struct sockaddr* addr = (const struct sockaddr*)serverAddr.getSockAddr();
        int ret = ::connect(sockfd, addr, sizeof(struct sockaddr_in));
        if (ret < 0 && errno != EINPROGRESS) {
            MFLOG_ERROR("RpcClient", "connect() failed: %s", strerror(errno));
            ::close(sockfd);
            return false;
        }

        // 创建 Channel 监听 EPOLLOUT
        auto* ctx = new ConnectContext;
        ctx->client     = this;
        ctx->sockfd     = sockfd;
        ctx->serverAddr = serverAddr;

        auto ch = std::make_unique<Channel>(loop_, sockfd);
        ch->setWriteCallback([this, ctx]() { onConnectWrite(ctx); });
        ch->enableWriting();

        ctx->channel = std::move(ch);
    }

    return true;
}

void RpcClient::disconnect() {
    for (auto& ch : channels_) {
        if (ch->conn && ch->conn->connected()) {
            ch->conn->shutdown();
        }
    }
    channels_.clear();
}

// ========== RPC 调用 ==========

RpcClient::RpcChannel* RpcClient::pickChannel() {
    if (channels_.empty()) return nullptr;

    size_t start = nextChannelIndex_;
    for (size_t i = 0; i < channels_.size(); ++i) {
        size_t idx = (start + i) % channels_.size();
        if (channels_[idx]->ready && channels_[idx]->conn && channels_[idx]->conn->connected()) {
            nextChannelIndex_ = (idx + 1) % channels_.size();
            return channels_[idx].get();
        }
    }
    return nullptr;
}

void RpcClient::asyncCall(const std::string& service,
                           const std::string& method,
                           const RpcValue& args,
                           SuccessCallback onSuccess,
                           ErrorCallback onError,
                           int /*timeoutMs*/) {
    RpcChannel* channel = pickChannel();
    if (!channel) {
        MFLOG_ERROR("RpcClient", "No available channel for %s.%s",
                    service.c_str(), method.c_str());
        if (onError) onError(-1, "No available connection");
        return;
    }

    uint32_t seqId = nextSeqId_.fetch_add(1);

    {
        PendingCall pc;
        pc.onSuccess = std::move(onSuccess);
        pc.onError   = std::move(onError);
        pendingCalls_[seqId] = std::move(pc);
    }

    std::vector<RpcValue> argsVec = {args};
    std::string payload = serializer_->buildRequest(service, method, argsVec);
    RpcMessage req = RpcCodec::makeRequest(seqId, service, method, payload);

    std::string encoded = RpcCodec::encode(req);
    channel->conn->send(encoded);
}

RpcValue RpcClient::syncCall(const std::string& service,
                              const std::string& method,
                              const RpcValue& args,
                              int timeoutMs) {
    std::promise<std::pair<bool, RpcValue>> promise;
    auto future = promise.get_future();

    asyncCall(service, method, args,
        [&promise](const RpcValue& result) {
            promise.set_value({true, result});
        },
        [&promise](int code, const std::string& msg) {
            std::string err = "{\"error\":{\"code\":" + std::to_string(code) +
                              ",\"message\":\"" + msg + "\"}}";
            promise.set_value({false, err});
        },
        timeoutMs
    );

    auto result = future.get();
    if (result.first) {
        auto parsed = serializer_->parseResponse(result.second);
        if (parsed.first) {
            return parsed.second;
        } else {
            MFLOG_ERROR("RpcClient", "syncCall response parse error: %s",
                        parsed.second.c_str());
            return RpcValue{};
        }
    }
    return RpcValue{};
}

// ========== 响应处理 ==========

void RpcClient::onMessage(RpcChannel* channel, const RpcMessage& msg) {
    switch (msg.type) {
        case RpcMessage::RESPONSE:
        case RpcMessage::ERROR: {
            auto it = pendingCalls_.find(msg.seqId);
            if (it == pendingCalls_.end()) {
                MFLOG_WARN("RpcClient", "No pending call for seq=%u", msg.seqId);
                return;
            }

            auto call = std::move(it->second);
            pendingCalls_.erase(it);

            if (msg.type == RpcMessage::RESPONSE) {
                if (call.onSuccess) {
                    auto parsed = serializer_->parseResponse(msg.payload);
                    if (parsed.first) {
                        call.onSuccess(parsed.second);
                    } else {
                        if (call.onError) call.onError(-3, "Parse error: " + parsed.second);
                    }
                }
            } else {
                if (call.onError) {
                    auto parsed = serializer_->parseResponse(msg.payload);
                    call.onError(-4, parsed.second);
                }
            }
            break;
        }
        case RpcMessage::HEARTBEAT: {
            if (channel->conn) {
                RpcMessage pong = RpcCodec::makeHeartbeat();
                channel->conn->send(RpcCodec::encode(pong));
            }
            break;
        }
        default:
            break;
    }
}

// ========== 配置与统计 ==========

void RpcClient::setHeartbeatInterval(int /*seconds*/) {
    // v1 暂不实现定时心跳
}

void RpcClient::setConnectedCallback(ConnectedCallback cb) {
    connectedCallback_ = std::move(cb);
}

int RpcClient::activeConnections() const {
    int count = 0;
    for (const auto& ch : channels_) {
        if (ch->ready && ch->conn && ch->conn->connected()) count++;
    }
    return count;
}

int RpcClient::pendingCalls() const {
    return static_cast<int>(pendingCalls_.size());
}

} // namespace myframework