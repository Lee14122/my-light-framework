# 项目背景
这是一个基于官方 muduo（chenshuo/muduo）构建的高性能 RPC 框架。

# 极其重要的依赖限制（绝不能违反）
1. 使用官方 muduo，所有网络层类型在 `muduo::net` / `muduo::base` 命名空间下，框架通过 `include/myframework/net/MuduoCompat.h` 统一引入别名到 `myframework` 命名空间。
2. 包含头文件时必须使用标准路径，如 `#include <muduo/net/EventLoop.h>`（在 MuduoCompat.h 中集中管理，其余文件只需 `#include "myframework/net/MuduoCompat.h"`）。
3. 绝对禁止修改 `/home/lee/projects/muduo-core` 目录下的任何代码；官方 muduo 源码位于 `/home/lee/projects/muduo`，安装到 `/home/lee/projects/muduo/install`，修改官方 muduo 需走正常 PR 流程。
4. 链接库为 `muduo_net` + `muduo_base`（静态库），不再使用 `muduo_core`。

# 框架目标
实现一个类似 gRPC 风格的高性能 RPC 远程调用框架。
- 自定义二进制协议：定长 28 字节帧头 + 可变长字段（service/method/meta/payload）
- 可插拔序列化：默认 JSON（调试）+ 二进制序列化（高性能）
- 连接池：客户端支持多连接并发调用（每连接一个 TcpClient）
- 自动重连：基于 TcpClient::enableRetry()
- 服务注册：server 端 serviceName → {methodName → handler}
- 心跳保活：基于 muduo EventLoop::runEvery 定时双向心跳
- 超时控制：基于 muduo EventLoop::runAfter 每次调用独立超时
- 所有模块统一在 `myframework` 命名空间下

# 架构概览
```
Application（启动入口）
  └── RpcServer（RPC 服务端）
        ├── RpcCodec（编解码 + 粘包拆包）
        │     └── RpcMessage（二进制帧协议）
        ├── Serializer（可插拔序列化）
        │     ├── JsonSerializer
        │     └── BinarySerializer
        └── TcpServerAdapter（封装 muduo TcpServer）
              │  ← Timestamp 透传（供 idle timeout 使用）

RpcClient（RPC 客户端）
  ├── TcpClient × N（muduo::net::TcpClient 连接池，含自动重连）
  ├── RpcCodec + Serializer
  ├── PendingCall Map（seqId → callback，含 runAfter 超时）
  └── Heartbeat Timer（基于 runEvery）
```

# 迁移记录（2026-11）
- 从 muduo-core（教学精简版）迁移到官方 muduo（chenshuo/muduo master）
- 核心变化：
  - 命名空间：全局 → muduo::net / muduo（框架内通过 MuduoCompat.h 别名引入）
  - 头文件：`"EventLoop.h"` → `<muduo/net/EventLoop.h>`
  - 链接库：`muduo_core` → `muduo_net + muduo_base`
  - 客户端：手写非阻塞 connect（ConnectContext/Channel/getsockopt）→ muduo::net::TcpClient
  - 定时器：无（runAfter/runEvery 不可用）→ 心跳/超时/idle timeout 全面落地
- muduo 安装脚本：`scripts/setup_muduo.sh`（一键克隆 + 编译 + 安装到 ~/projects/muduo/install）
- 删除文件：`include/myframework/net/Connection.h`（死脚手架）
- 删除代码：RpcClient 中 ConnectContext/onConnectWrite/裸 socket 连接 (~140 行)
- 删除成员：RpcCodec::pendingBuffer_/expectedLen_（无用死成员）