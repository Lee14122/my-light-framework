#include "myframework/rpc/RpcClient.h"
#include "myframework/Logger.h"

#include <chrono>
#include <future>
#include <mutex>

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

// ========================================================================
//  连接管理 — 基于 muduo::net::TcpClient（替换旧版手写非阻塞 connect）
// ========================================================================

bool RpcClient::connect(const std::string& host, uint16_t port) {
    return connectPool(host, port, 1);
}

bool RpcClient::connectPool(const std::string& host, uint16_t port, int numConnections) {
    // 官方 muduo: InetAddress(ip, port)，参数顺序与 muduo-core 相反
    InetAddress serverAddr(host, port);

    for (int i = 0; i < numConnections; ++i) {
        char nameBuf[64];
        snprintf(nameBuf, sizeof(nameBuf), "rpc-client-%zu", channels_.size());

        auto ch   = std::make_unique<RpcChannel>();
        auto tcpClient = std::make_unique<TcpClient>(loop_, serverAddr, nameBuf);
        tcpClient->enableRetry();   // 断线自动重连

        RpcChannel* rawCh = ch.get();
        RpcClient*  self  = this;

        // ---- 连接回调 ----
        tcpClient->setConnectionCallback(
            [self, rawCh](const TcpConnectionPtr& conn) {
                if (conn->connected()) {
                    rawCh->conn  = conn;
                    rawCh->state = ChannelState::IDLE;
                    rawCh->pendingCount = 0;
                    MFLOG_INFO("RpcClient", "Channel connected: %s", conn->name().c_str());
                    self->startHeartbeat(rawCh);
                    if (self->connectedCallback_) {
                        try {
                            self->connectedCallback_();
                        } catch (const std::exception& e) {
                            MFLOG_ERROR("RpcClient", "connectedCallback threw: %s", e.what());
                        } catch (...) {
                            MFLOG_ERROR("RpcClient", "connectedCallback threw unknown exception");
                        }
                    }
                } else {
                    // 断连：保存旧状态用于日志，精准清理本通道未完成调用
                    auto oldState = rawCh->state.load();
                    MFLOG_INFO("RpcClient", "Channel disconnected: %s (was state=%d)",
                               conn->name().c_str(), static_cast<int>(oldState));
                    self->failPendingCallsOnChannel(rawCh, static_cast<int>(RpcErrorCode::CONNECTION_LOST), "Channel disconnected");
                    rawCh->state = ChannelState::DEAD;
                    rawCh->pendingCount = 0;
                }
            });

        // ---- 消息回调 —— 交给 codec 解帧 ----
        auto codec = std::make_shared<RpcCodec>();
        codec->setSerializer(std::make_unique<JsonSerializer>());
        codec->setMessageCallback(
            [self, rawCh](const RpcMessage& msg) {
                self->onMessage(rawCh, msg);
            });

        tcpClient->setMessageCallback(
            [codec](const TcpConnectionPtr&, Buffer* buf, Timestamp) {
                codec->onData(buf);
            });

        ch->client = std::move(tcpClient);
        ch->codec  = codec;

        channels_.push_back(std::move(ch));

        // 发起连接（TcpClient 内部完成非阻塞 connect + Channel 管理）
        channels_.back()->client->connect();
    }

    MFLOG_INFO("RpcClient", "connectPool: %d connection(s) to %s:%u",
               numConnections, host.c_str(), port);
    return true;
}

void RpcClient::disconnect() {
    // 断开连接必须在 loop 线程执行（TcpClient 析构需要）
    if (!loop_->isInLoopThread()) {
        loop_->runInLoop([this]() { disconnect(); });
        return;
    }
    // 先清理每个通道：置 DEAD + fail 其未完成调用
    for (auto& ch : channels_) {
        loop_->cancel(ch->heartbeatTimer);
        ch->state = ChannelState::DEAD;
        failPendingCallsOnChannel(ch.get(), static_cast<int>(RpcErrorCode::CONNECTION_LOST), "Client disconnected");
    }
    channels_.clear();

    // 清理残留（理论上已被 failPendingCallsOnChannel 清空，兜底处理）
    for (auto& [seqId, call] : pendingCalls_) {
        loop_->cancel(call->timeoutTimer);
        if (call->onError) {
            call->onError(static_cast<int>(RpcErrorCode::CONNECTION_LOST), "Client disconnected");
        }
    }
    pendingCalls_.clear();
}

// ========================================================================
//  状态机辅助 — 断连清理 + BUSY→IDLE 归零
// ========================================================================

void RpcClient::failPendingCallsOnChannel(RpcChannel* channel, int code, const std::string& msg) {
    // 遍历 pendingCalls_，精准清理属于该通道的调用
    auto it = pendingCalls_.begin();
    while (it != pendingCalls_.end()) {
        if (it->second->channel == channel) {
            auto call = std::move(it->second);
            // 取消超时定时器
            if (call->hasTimeout) {
                loop_->cancel(call->timeoutTimer);
            }
            if (call->onError) {
                try {
                    call->onError(code, msg);
                } catch (const std::exception& e) {
                    MFLOG_ERROR("RpcClient", "onError callback threw: %s", e.what());
                } catch (...) {
                    MFLOG_ERROR("RpcClient", "onError callback threw unknown exception");
                }
            }
            it = pendingCalls_.erase(it);
        } else {
            ++it;
        }
    }
}

void RpcClient::decrementChannelPending(RpcChannel* channel) {
    if (!channel) return;
    channel->pendingCount--;
    if (channel->pendingCount <= 0) {
        channel->pendingCount = 0;
        // 仅当通道仍为 BUSY 时切回 IDLE（断连已置 DEAD 则不改）
        auto expected = ChannelState::BUSY;
        channel->state.compare_exchange_strong(expected, ChannelState::IDLE);
    }
}

// ========================================================================
//  心跳 — muduo::net::EventLoop::runEvery
// ========================================================================

void RpcClient::startHeartbeat(RpcChannel* channel) {
    if (heartbeatInterval_ <= 0) return;

    channel->heartbeatTimer = channel->client->getLoop()->runEvery(
        static_cast<double>(heartbeatInterval_),
        [channel]() {
            auto s = channel->state.load();
            if ((s == ChannelState::IDLE || s == ChannelState::BUSY) &&
                channel->conn && channel->conn->connected()) {
                RpcMessage hb = RpcCodec::makeHeartbeat();
                channel->conn->send(RpcCodec::encode(hb));
            }
        });
}

void RpcClient::setHeartbeatInterval(int seconds) {
    heartbeatInterval_ = seconds;
}

// ========================================================================
//  RPC 调用 — 线程安全的异步 + 超时
// ========================================================================

RpcClient::RpcChannel* RpcClient::pickChannel() {
    if (channels_.empty()) return nullptr;

    size_t start = nextChannelIndex_;

    // 第一趟：优先 IDLE 通道
    for (size_t i = 0; i < channels_.size(); ++i) {
        size_t idx = (start + i) % channels_.size();
        if (channels_[idx]->state == ChannelState::IDLE) {
            nextChannelIndex_ = (idx + 1) % channels_.size();
            return channels_[idx].get();
        }
    }

    // 第二趟：兜底 BUSY 通道（仍可 pipeline 新调用）
    for (size_t i = 0; i < channels_.size(); ++i) {
        size_t idx = (start + i) % channels_.size();
        if (channels_[idx]->state == ChannelState::BUSY) {
            nextChannelIndex_ = (idx + 1) % channels_.size();
            return channels_[idx].get();
        }
    }

    // CONNECTING / DEAD 均不可用
    return nullptr;
}

void RpcClient::asyncCall(const std::string& service,
                           const std::string& method,
                           const std::vector<RpcValue>& params, 
                           SuccessCallback onSuccess,
                           ErrorCallback onError,
                           int timeoutMs) {
    // 跨线程投递到 loop 线程，保证 pendingCalls_/channels_ 只在 loop 线程读写
    if (!loop_->isInLoopThread()) {
        loop_->runInLoop([this, service, method, params,
                          onSuccess = std::move(onSuccess),
                          onError   = std::move(onError),
                          timeoutMs]() mutable {
            doAsyncCall(service, method, params,
                        std::move(onSuccess), std::move(onError), timeoutMs);
        });
        return;
    }

    doAsyncCall(service, method, params, std::move(onSuccess), std::move(onError), timeoutMs);
}

void RpcClient::doAsyncCall(const std::string& service,
                             const std::string& method,
                             const std::vector<RpcValue>& params, 
                             SuccessCallback onSuccess,
                             ErrorCallback onError,
                             int timeoutMs) {
    RpcChannel* channel = pickChannel();
    if (!channel) {
        MFLOG_ERROR("RpcClient", "No available channel for %s.%s",
                    service.c_str(), method.c_str());
        if (onError) onError(static_cast<int>(RpcErrorCode::NO_AVAILABLE_CONNECTION), "No available connection");
        return;
    }

    // 首次分配调用时 IDLE → BUSY
    auto expected = ChannelState::IDLE;
    channel->state.compare_exchange_strong(expected, ChannelState::BUSY);
    channel->pendingCount++;

    uint32_t seqId = nextSeqId_.fetch_add(1);

    auto pc  = std::make_unique<PendingCall>();
    pc->seqId      = seqId;
    pc->channel    = channel;  // 记录归属通道
    pc->onSuccess  = std::move(onSuccess);
    pc->onError    = std::move(onError);

    // 超时定时器（runAfter）
    if (timeoutMs > 0) {
        pc->hasTimeout = true;
        RpcChannel* rawCh = channel;
        pc->timeoutTimer = loop_->runAfter(
            timeoutMs / 1000.0,
            [this, seqId, rawCh]() {
                auto it = pendingCalls_.find(seqId);
                if (it != pendingCalls_.end()) {
                    auto call = std::move(it->second);
                    pendingCalls_.erase(it);
                    MFLOG_WARN("RpcClient", "Request timeout seq=%u", seqId);
                    // 递减通道 pendingCount
                    decrementChannelPending(rawCh);
                    if (call->onError) {
                        try {
                            call->onError(static_cast<int>(RpcErrorCode::TIMEOUT), "Request timeout");
                        } catch (const std::exception& e) {
                            MFLOG_ERROR("RpcClient", "onError (timeout) callback threw: %s", e.what());
                        } catch (...) {
                            MFLOG_ERROR("RpcClient", "onError (timeout) callback threw unknown exception");
                        }
                    }
                }
            });
    }

    pendingCalls_[seqId] = std::move(pc);

    std::string payload = serializer_->buildRequest(service, method, params); 
    RpcMessage req = RpcCodec::makeRequest(seqId, service, method, payload);

    std::string encoded = RpcCodec::encode(req);
    channel->conn->send(encoded);
}

RpcValue RpcClient::syncCall(const std::string& service,
                              const std::string& method,
                              const std::vector<RpcValue>& params,
                              int timeoutMs) {
    std::promise<std::pair<bool, RpcValue>> promise;
    auto future = promise.get_future();

    asyncCall(service, method, params,
        [&promise](const RpcValue& result) {
            promise.set_value({true, result});
        },
        [&promise](int code, const std::string& msg) {
            // 错误也封装为 RpcValue
            RpcValue err{{"code", code}, {"message", msg}};
            promise.set_value({false, std::move(err)});
        },
        timeoutMs
    );

    // 兜底超时：比业务超时多等 500ms，防止底层定时器失灵
    int waitMs = (timeoutMs > 0 ? timeoutMs : 5000) + 500;
    auto status = future.wait_for(std::chrono::milliseconds(waitMs));
    if (status == std::future_status::timeout) {
        throw std::runtime_error("syncCall: future timed out (timer failed?)");
    }
    auto result = future.get();
    if (result.first) {
        // 回调收到的已是 parseResponse 提取后的 RpcValue，无需再次解析
        return result.second;
    }
    return RpcValue{};
}

// ========================================================================
//  响应处理
// ========================================================================

void RpcClient::onMessage(RpcChannel* /*channel*/, const RpcMessage& msg) {
    switch (msg.type) {
        case RpcMessage::RESPONSE:
        case RpcMessage::ERROR: {
            auto it = pendingCalls_.find(msg.seqId);
            if (it == pendingCalls_.end()) {
                MFLOG_DEBUG("RpcClient", "Late/duplicate response for seq=%u", msg.seqId);
                return;
            }

            auto call = std::move(it->second);
            pendingCalls_.erase(it);

            // 取消超时定时器（仅当设置了超时定时器时）
            if (call->hasTimeout) {
                loop_->cancel(call->timeoutTimer);
            }

            // 递减通道 pendingCount（归零时 BUSY → IDLE）
            decrementChannelPending(call->channel);

            if (msg.type == RpcMessage::RESPONSE) {
                if (call->onSuccess) {
                    auto parsed = serializer_->parseResponse(msg.payload);
                    if (parsed.ok) {
                        try {
                            call->onSuccess(parsed.value);
                        } catch (const std::exception& e) {
                            MFLOG_ERROR("RpcClient", "onSuccess callback threw: %s", e.what());
                        } catch (...) {
                            MFLOG_ERROR("RpcClient", "onSuccess callback threw unknown exception");
                        }
                    } else {
                        if (call->onError) {
                            try {
                                call->onError(parsed.error.code, parsed.error.message);
                            } catch (const std::exception& e) {
                                MFLOG_ERROR("RpcClient", "onError callback threw: %s", e.what());
                            } catch (...) {
                                MFLOG_ERROR("RpcClient", "onError callback threw unknown exception");
                            }
                        }
                    }
                }
            } else {
                if (call->onError) {
                    auto parsed = serializer_->parseResponse(msg.payload);
                    try {
                        call->onError(parsed.error.code, parsed.error.message);
                    } catch (const std::exception& e) {
                        MFLOG_ERROR("RpcClient", "onError callback threw: %s", e.what());
                    } catch (...) {
                        MFLOG_ERROR("RpcClient", "onError callback threw unknown exception");
                    }
                }
            }
            break;
        }
        case RpcMessage::HEARTBEAT: {
            // 服务端也可能发心跳过来，回复 pong
            MFLOG_DEBUG("RpcClient", "Heartbeat received");
            break;
        }
        default:
            break;
    }
}

// ========================================================================
//  配置与统计
// ========================================================================

void RpcClient::setConnectedCallback(ConnectedCallback cb) {
    connectedCallback_ = std::move(cb);
}

int RpcClient::activeConnections() const {
    int count = 0;
    for (const auto& ch : channels_) {
        auto s = ch->state.load();
        if (s == ChannelState::IDLE || s == ChannelState::BUSY) count++;
    }
    return count;
}

int RpcClient::pendingCalls() const {
    return static_cast<int>(pendingCalls_.size());
}

} // namespace myframework