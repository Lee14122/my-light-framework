#pragma once

#include <memory>
#include <functional>
#include "myframework/net/MuduoCompat.h"

namespace myframework {

/**
 * 基础服务器适配层，封装 muduo TcpServer。
 * 负责连接建立和字节流分发到对应的协议解析器。
 */
class TcpServerAdapter {
public:
    // 对外回调类型（保持 Timestamp 透传，供 idle timeout 等使用）
    using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;
    using MessageCallback    = std::function<void(const TcpConnectionPtr&, Buffer*, Timestamp)>;

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