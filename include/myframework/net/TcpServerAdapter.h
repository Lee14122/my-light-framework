#pragma once

#include <memory>
#include "TcpServer.h"
#include "Callbacks.h"

namespace myframework {

class Connection;

/**
 * 基础服务器适配层，封装 muduo TcpServer。
 * 负责连接建立和字节流分发到对应的协议解析器。
 */
class TcpServerAdapter {
public:
    TcpServerAdapter(EventLoop* loop, const InetAddress& addr, const std::string& name);
    ~TcpServerAdapter();

    void setThreadNum(int num);
    void start();

private:
    void onConnection(const TcpConnectionPtr& conn);
    void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp time);

    TcpServer server_;
    // TODO: Connection map
};

} // namespace myframework