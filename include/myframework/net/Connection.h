#pragma once

#include <memory>
#include "TcpConnection.h"

namespace myframework {

class HttpParser;
class RpcCodec;

/**
 * 连接上下文：每个 TcpConnection 绑定一个协议解析器实例。
 */
class Connection {
public:
    explicit Connection(const TcpConnectionPtr& tcpConn);
    ~Connection();

    TcpConnectionPtr tcpConnection() const;

    // 协议解析器绑定（惰性创建）
    HttpParser*  httpParser();
    RpcCodec*    rpcCodec();

private:
    TcpConnectionPtr tcpConn_;
    // TODO: std::unique_ptr<HttpParser> httpParser_;
    // TODO: std::unique_ptr<RpcCodec>   rpcCodec_;
};

} // namespace myframework