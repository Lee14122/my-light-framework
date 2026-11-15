#pragma once

// ============================================================================
// MuduoCompat.h — 将 muduo 的核心类型别名引入 myframework 命名空间
//
// 官方 muduo (chenshuo/muduo) 的所有类型都在 muduo::net / muduo::base
// 命名空间中。通过本文件集中 using，框架其余代码无需前缀即可使用。
// ============================================================================

#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpConnection.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/Callbacks.h>
#include <muduo/net/EventLoopThreadPool.h>
#include <muduo/base/Timestamp.h>

namespace myframework {

// --- muduo::net ---
using muduo::net::EventLoop;
using muduo::net::TcpServer;
using muduo::net::TcpClient;
using muduo::net::TcpConnection;
using muduo::net::TcpConnectionPtr;
using muduo::net::Buffer;
using muduo::net::InetAddress;

// --- muduo::base ---
using muduo::Timestamp;

} // namespace myframework