# 项目背景
这是一个基于 muduo-core（代码随想录精简版）构建的高性能 RPC 框架。

# 极其重要的依赖限制（绝不能违反）
1. `muduo-core` 没有命名空间（如 `muduo::` 或 `muduo::net::`），所有类直接暴露在全局作用域，如 `EventLoop`、`TcpServer`。
2. 包含头文件时必须扁平化，如 `#include <EventLoop.h>`，绝不能写 `#include <muduo/net/EventLoop.h>`。
3. 绝对禁止修改 `/home/lee/projects/muduo-core` 目录下的任何代码，只能将其作为外部库通过 CMake 链接。

# 框架目标
实现一个类似 gRPC 风格的高性能 RPC 远程调用框架。
- 自定义二进制协议：定长帧头 + 可变长字段（service/method/meta/payload）
- 可插拔序列化：默认 JSON（调试）+ 二进制序列化（高性能）
- 连接池：客户端支持多连接并发调用
- 服务注册：server 端 serviceName → {methodName → handler}
- 心跳保活：客户端-服务端双向心跳
- 超时控制：每次调用独立超时
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
              └── Connection（连接上下文）

RpcClient（RPC 客户端）
  ├── RpcChannel × N（连接池，round-robin 调度）
  ├── RpcCodec + Serializer
  └── PendingCall Map（seqId → callback，支持超时）
```