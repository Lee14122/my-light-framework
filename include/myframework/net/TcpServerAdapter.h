#pragma once

#include <memory>
#include <functional>
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
    // 对外回调类型
    using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;
    using MessageCallback    = std::function<void(const TcpConnectionPtr&, Buffer*)>;

    TcpServerAdapter(EventLoop* loop, const InetAddress& addr, const std::string& name);
    ~TcpServerAdapter();

    void setThreadNum(int num);
    void start();

    // 设置上层回调
    void setConnectionCallback(ConnectionCallback cb);
    void setMessageCallback(MessageCallback cb);

private:
    void onConnection(const TcpConnectionPtr& conn);
    void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp time);

    TcpServer server_;
    ConnectionCallback connectionCallback_;
    MessageCallback    messageCallback_;
};

} // namespace myframework