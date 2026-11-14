#include "myframework/net/TcpServerAdapter.h"
#include "myframework/Logger.h"

namespace myframework {

TcpServerAdapter::TcpServerAdapter(EventLoop* loop,
                                    const InetAddress& addr,
                                    const std::string& name)
    : server_(loop, addr, name)
{
    server_.setConnectionCallback(
        [this](const TcpConnectionPtr& conn) {
            this->onConnection(conn);
        });
    server_.setMessageCallback(
        [this](const TcpConnectionPtr& conn, Buffer* buf, Timestamp time) {
            this->onMessage(conn, buf, time);
        });
}

TcpServerAdapter::~TcpServerAdapter() = default;

void TcpServerAdapter::setThreadNum(int num) {
    server_.setThreadNum(num);
}

void TcpServerAdapter::start() {
    server_.start();
}

void TcpServerAdapter::setConnectionCallback(ConnectionCallback cb) {
    connectionCallback_ = std::move(cb);
}

void TcpServerAdapter::setMessageCallback(MessageCallback cb) {
    messageCallback_ = std::move(cb);
}

void TcpServerAdapter::onConnection(const TcpConnectionPtr& conn) {
    if (conn->connected()) {
        MFLOG_INFO("TcpServerAdapter", "New connection: %s -> %s",
                   conn->peerAddress().toIpPort().c_str(),
                   conn->localAddress().toIpPort().c_str());
        if (connectionCallback_) {
            connectionCallback_(conn);
        }
    } else {
        MFLOG_INFO("TcpServerAdapter", "Connection closed: %s",
                   conn->name().c_str());
    }
}

void TcpServerAdapter::onMessage(const TcpConnectionPtr& conn,
                                  Buffer* buf, Timestamp /*time*/) {
    if (messageCallback_) {
        messageCallback_(conn, buf);
    }
}

} // namespace myframework