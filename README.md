# My Light Framework - 基于C++11的高性能RPC框架

## 项目简介

My Light Framework 是一个基于 C++11 标准开发的高性能 RPC（Remote Procedure Call）框架，底层采用 epoll I/O 多路复用机制和 Reactor 事件驱动模型。该框架旨在提供类似 gRPC 的远程调用体验，支持自定义二进制协议、可插拔序列化、连接池、服务注册与发现、心跳保活等企业级特性。

本项目为本科毕业设计项目，基于官方 muduo (chenshuo/muduo) 网络库，自底向上构建一个完整的、高性能的 RPC 框架。

**项目有两个可选发展方向**（详见「项目方向与路线图」章节）：
- **方向 A**：功能完整的轻量级 C++ RPC 框架（对标 gRPC/brpc 简化版）
- **方向 B**：面向局域网的可编程高性能传输引擎（聚焦数据搬运 + 高性能数据通路）

> 当前处于**第一阶段（公共地基）**，完成 7 个必做项后再选择具体方向。

## 技术栈

- **编程语言**：C++17
- **网络库**：muduo (chenshuo/muduo) — `muduo::net` / `muduo::base` 命名空间
- **I/O 模型**：epoll + Reactor 模式（one loop per thread）
- **构建系统**：CMake 3.10+
- **开发环境**：Linux (WSL2), GCC 15+
- **序列化**：JSON（开发调试）/ 二进制（高性能）
- **测试工具**：webbench / wrk（性能压测）

## 项目特性

- ✅ **事件驱动架构**：基于 epoll 的高效事件循环，one-loop-per-thread 多 Reactor 模型
- ✅ **自定义二进制协议**：定长 28 字节帧头 + 可变长字段，高效紧凑
- ✅ **可插拔序列化**：默认 JSON（易调试），可切换二进制序列化（高性能），未来可对接 Protobuf
- ✅ **连接池**：客户端支持多连接并发调用，round-robin 负载均衡
- ✅ **服务注册与发现**：server 端 serviceName → {methodName → handler} 映射
- ✅ **超时控制**：每次 RPC 调用独立超时，防止资源泄漏
- ✅ **心跳保活**：客户端-服务端双向心跳，自动检测断连
- ✅ **同步/异步调用**：同时支持 syncCall（阻塞）和 asyncCall（回调）
- ✅ **清晰的模块化设计**：各模块职责分离，接口清晰，易于扩展

## 架构设计

### 整体架构

```
┌──────────────────────────────────────────┐
│              Application                 │
│           (框架启动入口)                   │
└────────────────┬─────────────────────────┘
                 │
    ┌────────────▼─────────────────────────┐
    │            RpcServer                 │
    │    (服务注册 + 请求分发 + 心跳处理)     │
    │                                       │
    │  services_:                          │
    │    "UserService" → {                 │
    │      "Get"  → handler,               │
    │      "List" → handler                │
    │    }                                 │
    └────────┬─────────────────────────────┘
             │
    ┌────────▼────────────┐
    │     RpcCodec        │
    │  (编解码 + 粘包拆包) │
    └────────┬────────────┘
             │
    ┌────────▼────────────┐    ┌──────────────┐
    │   TcpServerAdapter  │    │  Serializer  │
    │ (封装 muduo TcpServer)│   │ (可插拔序列化) │
    └────────┬────────────┘    └──────────────┘
             │
    ┌────────▼────────────┐
    │   muduo::net        │
    │ EventLoop / TcpServer│
    │ Buffer / Channel     │
    └─────────────────────┘


┌──────────────────────────────────────────┐
│            RpcClient                     │
│  (连接池 + 同步/异步调用 + 超时管理)       │
│                                           │
│  channels_: [RpcChannel×n]  (round-robin) │
│  pendingCalls_: seqId → callback          │
└────────┬─────────────────────────────────┘
         │
┌────────▼────────────┐
│    RpcChannel       │
│  (单连接 + RpcCodec) │
└─────────────────────┘
```

### RPC 调用流程

```
Client                          Server
  │                                │
  │  syncCall("User","Get",args)   │
  │                                │
  ├──1. encode RpcMessage─────────►│
  │     (HEADER + service + method │
  │      + payload)                │
  │                                │
  │                             2. RpcCodec 拆包
  │                             3. dispatch("User","Get")
  │                             4. 调用 handler(RpcContext)
  │                             5. encode response
  │                                │
  │◄──6. response RpcMessage──────┤
  │                                │
  │  7. decode                    │
  │  8. 匹配 seqId → callback     │
  │  9. 返回 result               │
  │                                │
```

### 二进制协议帧格式

```
┌──────────┬──────────┬──────────┬──────────┬──────────┐
│  magic   │ totalLen │ hdrLen   │  type    │ compress │
│ 4 bytes  │ 4 bytes  │ 2 bytes  │ 1 byte   │ 1 byte   │
├──────────┼──────────┼──────────┼──────────┼──────────┤
│  seqId   │ svcNameLen│methNameLen│ metaLen │payloadLen│
│ 4 bytes  │ 2 bytes   │ 2 bytes   │ 2 bytes │ 4 bytes  │
├──────────┴──────────┴──────────┴──────────┴──────────┤
│   service_name [svcNameLen]                           │
│   method_name  [methNameLen]                          │
│   meta         [metaLen]   (trace-id 等透传字段)        │
│   payload      [payloadLen]                           │
└───────────────────────────────────────────────────────┘

帧头固定 28 字节，后跟 4 段可变长数据
```

## 目录结构

```
my-light-framework/
├── CMakeLists.txt                    # CMake 构建配置
├── README.md                         # 项目说明文档（本文件）
├── CLAUDE.md                         # 项目架构说明（AI 辅助）
│
├── include/                          # 头文件目录
│   └── myframework/
│       ├── Logger.h                  # 日志模块
│       ├── Config.h                  # 配置模块
│       │
│       ├── net/                      # 网络层
│       │   ├── TcpServerAdapter.h    # TCP 服务器适配层
│       │   └── Connection.h          # 连接上下文
│       │
│       ├── rpc/                      # RPC 子系统
│       │   ├── RpcMessage.h          # RPC 二进制协议帧定义
│       │   ├── Serializer.h          # 可插拔序列化器
│       │   ├── RpcCodec.h            # RPC 编解码器（拆包粘包）
│       │   ├── RpcServer.h           # RPC 服务端
│       │   └── RpcClient.h           # RPC 客户端（连接池）
│       │
│       └── app/                      # 应用层
│           └── Application.h         # 框架启动入口
│
└── src/                              # 源文件目录
    ├── main.cpp                      # 主程序入口
    ├── net/                          # 网络层实现
    ├── rpc/                          # RPC 子系统实现
    └── app/                          # 应用层实现
```

## 模块详细说明

### 1. 公共模块

#### Logger（日志模块）
- **文件**：`include/myframework/Logger.h`
- **职责**：提供多级别日志输出（DEBUG/INFO/WARN/ERROR/FATAL），带时间戳和模块标签
- **接口**：`MFLOG_INFO(tag, fmt, ...)` / `MFLOG_ERROR(tag, fmt, ...)` 等宏

#### Config（配置模块）
- **文件**：`include/myframework/Config.h`
- **职责**：集中管理框架配置（RPC 端口、线程数、超时时间等）
- **设计**：Builder 模式，支持链式调用

### 2. 网络层

#### TcpServerAdapter（TCP 服务器适配层）
- **文件**：`include/myframework/net/TcpServerAdapter.h`
- **职责**：封装 muduo 的 `TcpServer`，管理 TCP 连接的生命周期
- **设计**：在 `MessageCallback` 中将原始 `Buffer*` 数据交给上层 RPC 编解码器

#### Connection（连接上下文）
- **文件**：`include/myframework/net/Connection.h`
- **职责**：为每个 `TcpConnection` 关联一个 `RpcCodec` 实例，维护连接的解析状态

### 3. RPC 子系统

#### RpcMessage（RPC 二进制协议帧）
- **文件**：`include/myframework/rpc/RpcMessage.h`
- **职责**：定义 RPC 通信的二进制帧格式
- **设计**：
  - 定长 28 字节帧头（magic + totalLen + type + seqId + 各字段长度）
  - 4 段可变长数据：service_name / method_name / meta / payload
  - 支持 REQUEST / RESPONSE / ERROR / HEARTBEAT 四种消息类型

#### Serializer（可插拔序列化器）
- **文件**：`include/myframework/rpc/Serializer.h`
- **职责**：抽象序列化层，支持切换不同序列化实现
- **内置实现**：
  - `JsonSerializer`：JSON 格式，方便开发调试
  - `BinarySerializer`：二进制编码，追求极致性能
- **扩展**：用户可实现 `ProtobufSerializer` 等

#### RpcCodec（RPC 编解码器）
- **文件**：`include/myframework/rpc/RpcCodec.h`
- **职责**：处理 TCP 字节流的粘包/拆包，将字节流转换为 `RpcMessage` 结构体
- **设计**：
  - 内部维护 `pendingBuffer_` 缓存未完整帧
  - 根据帧头 `totalLen` 字段判断帧边界
  - 收到完整帧后通过回调通知上层

#### RpcServer（RPC 服务端）
- **文件**：`include/myframework/rpc/RpcServer.h`
- **职责**：管理服务注册表，接收请求并分发到对应 handler，返回响应
- **核心接口**：
  ```cpp
  void registerService(const std::string& serviceName,
                       std::unordered_map<std::string, MethodHandler> methods);
  ```
- **功能**：
  - 服务注册与查找
  - 请求分发（service + method → handler）
  - 心跳自动响应
  - 超时管理

#### RpcClient（RPC 客户端）
- **文件**：`include/myframework/rpc/RpcClient.h`
- **职责**：发起远程 RPC 调用，管理连接池和待处理请求
- **核心接口**：
  ```cpp
  RpcValue syncCall(service, method, args, timeoutMs);
  void asyncCall(service, method, args, onSuccess, onError, timeoutMs);
  ```
- **功能**：
  - 连接池（多连接并发，round-robin 调度）
  - 同步/异步调用
  - 每个调用独立超时
  - 心跳保活
  - seqId → callback 映射，匹配请求与响应

### 4. 应用层

#### Application（框架启动器）
- **文件**：`include/myframework/app/Application.h`
- **职责**：一站式启动入口，读取 Config，初始化 RpcServer，启动 EventLoop
- **用法**：
  ```cpp
  Application app;
  app.init(Config::build().rpcPort(9090).threadNum(4).build());
  app.rpcServer().registerService("UserService", {...});
  app.run();
  ```

## 构建与运行

### 环境要求

- Linux 环境（推荐 WSL2 或原生 Linux）
- C++17 兼容编译器（GCC 8+ 或 Clang 7+）
- CMake 3.10+
- pthread 库

### 前置依赖：编译官方 muduo

```bash
# 一键安装脚本（克隆 + CMake 配置 + 编译 + 安装）
bash scripts/setup_muduo.sh

# 或手动编译（安装到 ~/projects/muduo/install）
git clone https://github.com/chenshuo/muduo ~/projects/muduo
cd ~/projects/muduo
cmake -S . -B build -DCMAKE_BUILD_TYPE=release \
    -DBUILD_EXAMPLES=OFF -DBUILD_TEST=OFF \
    -DCMAKE_INSTALL_PREFIX=$HOME/projects/muduo/install
cmake --build build -j$(nproc)
cmake --install build
# 产物: include/muduo/net/*.h, lib/libmuduo_{net,base}.a
```

### 构建步骤

```bash
# 1. 进入项目目录
cd /home/lee/projects/my-light-framework

# 2. 创建构建目录
mkdir build && cd build

# 3. 配置 CMake
cmake ..

# 4. 编译
make -j$(nproc)

# 5. 运行
./server
```

## 使用示例

### RPC 服务端

```cpp
#include <myframework/app/Application.h>
#include <myframework/rpc/RpcServer.h>

std::string getUserById(const std::string& id) {
    // 模拟数据库查询
    return R"({"id":")" + id + R"(","name":"Alice","age":25})";
}

int main() {
    myframework::Application app;

    // 初始化
    app.init(myframework::Config::build()
        .rpcPort(9090)
        .threadNum(4)
        .build());

    // 注册服务
    app.rpcServer().registerService("UserService", {
        {"GetUser", [](RpcContext& ctx) {
            std::string result = getUserById(ctx.args);
            ctx.reply(result);
        }},
        {"ListUsers", [](RpcContext& ctx) {
            ctx.reply(R"([{"id":"1","name":"Alice"},{"id":"2","name":"Bob"}])");
        }},
        {"DeleteUser", [](RpcContext& ctx) {
            // 业务逻辑...
            ctx.reply(R"({"success":true})");
        }}
    });

    // 启动（阻塞）
    app.run();
    return 0;
}
```

### RPC 客户端（同步调用）

```cpp
#include <myframework/rpc/RpcClient.h>
#include <iostream>

int main() {
    EventLoop loop;
    myframework::RpcClient client(&loop);

    // 建立连接池（4 个并发连接）
    client.connectPool("127.0.0.1", 9090, 4);

    // 同步调用
    myframework::RpcValue result = client.syncCall(
        "UserService", "GetUser", R"("1")",
        3000  // 3 秒超时
    );

    std::cout << "Result: " << result << std::endl;

    // 批量调用
    for (int i = 0; i < 10; i++) {
        result = client.syncCall("UserService", "ListUsers", "");
        std::cout << "Page " << i << ": " << result << std::endl;
    }

    client.disconnect();
    return 0;
}
```

### RPC 客户端（异步调用）

```cpp
#include <myframework/rpc/RpcClient.h>
#include <iostream>

int main() {
    EventLoop loop;

    myframework::RpcClient client(&loop);
    client.connectPool("127.0.0.1", 9090, 4);

    // 异步调用
    client.asyncCall(
        "UserService", "GetUser", R"("42")",
        // 成功回调
        [](const myframework::RpcValue& result) {
            std::cout << "Got user: " << result << std::endl;
        },
        // 错误回调
        [](int code, const std::string& msg) {
            std::cerr << "Error " << code << ": " << msg << std::endl;
        },
        3000  // 超时 3 秒
    );

    // 同时发起多个异步请求
    for (int i = 0; i < 5; i++) {
        client.asyncCall("UserService", "ListUsers", "",
            [i](const myframework::RpcValue& r) {
                std::cout << "Async call " << i << " done" << std::endl;
            },
            [](int c, const std::string& m) {
                std::cerr << "Failed: " << m << std::endl;
            });
    }

    loop.loop();  // 进入事件循环
    return 0;
}
```

## 开发进度

### 已完成
- [x] 项目架构设计与模块划分
- [x] 开发环境搭建（WSL + CMake + muduo）
- [x] 底层网络库链接验证（EventLoop 创建测试通过）
- [x] 框架骨架代码编写（所有模块头文件定义完成）

### 开发计划（按 Phase 顺序）

| Phase | 模块 | 说明 |
|-------|------|------|
| 1 | Logger + Config | 零依赖，立即可用 |
| 2 | TcpServerAdapter + Connection | 打通 muduo → 框架的链路 |
| 3 | RpcMessage | 二进制协议帧定义 + 编解码 |
| 4 | Serializer | 可插拔序列化（JSON + Binary） |
| 5 | RpcCodec | 粘包拆包处理 |
| 6 | RpcServer | 服务注册 + 请求分发 + 心跳 |
| 7 | RpcClient | 连接池 + sync/async 调用 |
| 8 | Application | 统一启动入口 + demo |

### 当前阶段：公共地基

> 详见上方「第一阶段：公共地基（7 个必做项）」。这 7 项是方向 A 和方向 B 的共同前提，做完才具备起跑线。

- [ ] **必做项 1**：真正的序列化（`nlohmann/json` 集成）
- [ ] **必做项 2**：变参传参 + 服务端解析 params
- [ ] **必做项 3**：协议 round-trip 测试 + 核对 HEADER_SIZE
- [ ] **必做项 4**：超时真正生效
- [ ] **必做项 5**：连接池状态机（IDLE/BUSY/CONNECTING/DEAD）
- [ ] **必做项 6**：错误码统一 + 异常安全
- [ ] **必做项 7**：配置校验 + logLevel 生效

### 后续规划

- [ ] 选择方向 A 或方向 B 深入（见上方路线图）
- [ ] 性能压测（wrk / webbench）
- [ ] 性能分析与优化
- [ ] 毕业论文撰写

## 项目方向与路线图

本项目有两个可选发展方向，当前阶段先完成两者的**公共地基**（7 个必做项），之后再选择具体方向深入。

---

### 方向 A：功能完整的轻量级 C++ RPC 框架

**一句话定位**：对标 gRPC/brpc 的简化版，展示对「分布式 RPC 完整链路」的系统性理解。

**简历描述（可直接用）**：

> **自研轻量级 C++ RPC 框架**（epoll + 自定义二进制协议）
> - 设计**长度前缀 + 大端序**二进制帧格式，解决 TCP 粘包/拆包，`peek + retrieve` 实现单帧零拷贝解码。
> - 实现**可插拔序列化层**：JSON / Protobuf 双实现，接口抽象，切换序列化不改上层。
> - 实现**连接池状态机**（IDLE/BUSY/CONNECTING/DEAD）：空闲复用、繁忙 pipeline、死亡自动重连，压测 P99 大幅下降。
> - 实现**三阶段超时**（连接/发送/响应）+ 定时器驱动，超时清理 pending 调用，杜绝 `future.get()` 永久挂死。
> - 集成 **etcd 服务发现** + **轮询/一致性哈希负载均衡**，支持实例动态上下线。
> - 实现 **Stub/Proxy 层**：IDL 生成客户端代理，调用方写 `EchoService::echo("hi")` 而非手拼字符串。

**面试叙事**：「我实现了一个功能完整的 RPC 框架，覆盖从协议设计、序列化、连接管理到服务发现、负载均衡的完整链路。核心挑战是 **P99 延迟控制**——通过连接池状态机 + 三阶段超时把尾延迟压下来，用压测数据量化了每个优化的收益。」

**适合的岗位**：后端 C++ 开发、基础架构、中间件。

**这个方向的风险**：功能清单很长，但每一项都有成熟对标（gRPC 已经做得很好），面试官容易问「你和 gRPC 比有什么优势」，需要有清晰的取舍叙事。

---

### 方向 B：面向局域网的可编程高性能传输引擎

**一句话定位**：对标 libtorrent / librats 的「传输内核」定位，聚焦「数据搬运」场景，展示对**高性能数据通路**和**可编程任务编排**的深度。

**简历描述（可直接用）**：

> **面向局域网的可编程高性能传输引擎**（C++ / epoll）
> - 以自研 RPC 帧协议为地基，聚焦「数据从 A 到 B」的传输场景，提供可嵌入、可编排的传输内核。
> - 实现**高性能数据通路**：零拷贝发送（`sendfile`/`splice`）、内存池复用缓冲区、批量 `writev` 合并小包，大文件吞吐达线速。
> - 实现**背压控制**：监控连接 output buffer 水位，高水位暂停上游读取、低水位恢复，杜绝慢接收端导致的内存无限增长。
> - 设计**声明式传输任务 API**：`TransferTask{source, dest, policy}`，引擎负责分块、压缩、断点续传、重试、进度回调，调用方只声明「要什么」。
> - 实现**传输策略**：断点续传（kill 连接后从 50% 继续）、增量传输（1MB 文件改 1KB 只传 KB 级）、内容感知分块与压缩。
> - 设计**可插拔子系统**（发现/压缩/分块/校验独立可替换），提供 C API 供其他程序嵌入。

**面试叙事**：「我发现通用 RPC 框架和通用文件传输工具之间存在一个缝隙——**局域网内、需要进入自动化流程、且要求极致吞吐和背压控制**的场景。所以我以 RPC 帧协议为地基，做了高性能数据通路（零拷贝 + 背压）和可编程任务编排（声明式 API + 断点续传 + 增量传输），并用 benchmark 证明它在『大文件吞吐』和『高并发小消息』两个维度上的表现。」

**适合的岗位**：基础架构、存储/传输、嵌入式中间件、音视频基础设施。

**这个方向的风险**：需要真正理解零拷贝、背压、增量传输这些机制，面试官会深挖「零拷贝到底省了几次拷贝」「背压的水位怎么定」。但**一旦讲清楚，辨识度远高于方向 A**。

---

### 策略建议：A + B，以 B 为叙事主线

**用 A 的地基，讲 B 的故事。** 必做项 1–7 是 A 的地基；零拷贝、背压、声明式 API、断点续传是 B 的核心卖点。**A 让「地基可信」，B 让「有差异化」。**

---

## 第一阶段：公共地基（7 个必做项）

以下 7 项是方向 A 和方向 B 的共同前提，**做完才具备起跑线**。

| # | 必做项 | 现状问题 | 验收标准 |
|---|--------|----------|----------|
| 1 | **真正的序列化** | `JsonSerializer::serialize` 直接 `return val`；`buildRequest` 手拼字符串；`parseResponse` 用 `find("\"error\"")` 判断错误 | 引入 `nlohmann/json`（或 `rapidjson`），实现真正的 `serialize`/`deserialize`；`echo("hello")` 完整往返无嵌套 JSON |
| 2 | **变参传参 + 服务端解析 params** | `argsVec = {args}` 永远单元素；`dispatch` 里 `ctx.args = msg.payload` 整包塞入 | `asyncCall` 支持变参；`RpcContext` 增加 `argsArray`；能调 `CalcService.add(3, 5)` 返回 `8` |
| 3 | **协议 round-trip 测试 + 核对 HEADER_SIZE** | `encode` 注释说 offset=28，逐字节累加实际是 26；可能帧头末尾 2 字节错位 | 单元测试：`RpcMessage → encode → decode →` 断言所有字段一致；`totalLen == frame.size()` |
| 4 | **超时真正生效** | `asyncCall` 的 `timeoutMs` 被注释忽略；`syncCall` 的 `future.get()` 无超时 | 服务端 handler sleep 10s，客户端 `timeoutMs=1000`，1s 后收到超时错误，`pendingCalls_` 被清理 |
| 5 | **连接池状态机** | `pickChannel` 只做简单轮询，不区分连接状态，不探活，不重连 | 每条 `RpcChannel` 加状态：`IDLE`/`BUSY`/`CONNECTING`/`DEAD`；kill 连接自动重连；不会选到 `DEAD` 连接 |
| 6 | **错误码统一 + 异常安全** | 错误码散落魔数（-1/-2/-3/-4）；`syncCall` 拼错误 JSON 不转义；`ConnectContext` 手动 `delete`；用户回调抛异常会炸 loop | 定义 `enum class RpcErrorCode`；所有错误路径有明确错误码；ASan 无泄漏；回调抛异常不影响 loop |
| 7 | **配置校验 + logLevel 生效** | `build()` 不校验；`logLevel` 字段可能没应用到 `Logger` | 非法配置 `build()` 报错；`logLevel="ERROR"` 时 INFO 日志不输出 |

### 每项为什么是前提

| 项目 | 做完后能回答的面试问题 |
|------|------------------------|
| 真序列化 | 「你的序列化怎么实现的？为什么不用 JSON 的字符串匹配判断错误？」 |
| 变参传参 | 「多参数方法怎么调？」 |
| 协议 round-trip | 「你怎么保证帧编解码正确？」 |
| 超时 | 「网络断了 `syncCall` 会怎样？」 |
| 连接池状态机 | 「连接池怎么管理？P99 怎么压？」 |
| 错误码 + 异常安全 | 「用户回调抛异常怎么办？」 |
| 配置校验 | 「非法配置怎么处理？」 |

### 做完 7 项后的分岔

```
               ┌── 公共地基（7 项）──┐
               │                    │
               ▼                    ▼
          方向 A                  方向 B
  - Stub/Proxy 层              - 零拷贝（sendfile/splice）
  - etcd 服务发现               - 背压控制
  - 一致性哈希负载均衡           - 声明式 TransferTask API
  - 压测 + P99 优化             - 断点续传 / 增量传输
                               - 可插拔子系统 + C API
```

> **建议从必做项 1（真序列化）开始**，因为它牵动 `Serializer.cc`、`RpcServer::dispatch`、`RpcContext` 三处，是后面所有改动的基础。

---

## 关键设计决策

### 为什么不用 gRPC?

gRPC 基于 HTTP/2 + Protobuf，功能强大但依赖较重。本项目的目标是：

1. **学习价值**：从底层 epoll/Buffer/EventLoop 开始，自顶向下构建完整的 RPC 框架，深入理解网络编程原理
2. **轻量化**：总代码量控制在 5000 行以内，便于阅读和维护
3. **定制性**：可灵活定制协议格式、序列化方式、负载均衡策略

### 协议设计选择

- **自定义二进制协议**（而非 HTTP/JSON）：帧头紧凑（28 字节），无冗余 HTTP 头开销，性能更好
- **JSON 序列化**作为默认：方便开发调试，未来可切换到二进制序列化获得更高性能

### muduo 依赖

- 使用官方 muduo (chenshuo/muduo)，安装到 `~/projects/muduo/install`
- 命名空间：`muduo::net` / `muduo::base`，框架通过 `MuduoCompat.h` 引入别名
- 链接库：`muduo_net` + `muduo_base`（静态库）
- muduo-core 为历史依赖，已不再链接

## 参考文献

1. 陈硕. Linux多线程服务端编程：使用muduo C++网络库[M]. 北京: 电子工业出版社, 2015.
2. W. Richard Stevens, Bill Fenner, Andrew M. Rudoff. UNIX网络编程 卷1：套接字联网API（第3版）[M]. 北京: 人民邮电出版社, 2010.
3. W. Richard Stevens. TCP/IP详解 卷1：协议[M]. 北京: 机械工业出版社, 2004.
4. Scott Meyers. Effective C++：改善程序与设计的55个具体做法（第3版）[M]. 北京: 电子工业出版社, 2011.
5. Bjarne Stroustrup. C++程序设计原理与实践（原书第2版）[M]. 北京: 机械工业出版社, 2017.
6. gRPC 官方文档. https://grpc.io/docs/
7. Kasun Indrasiri, Danesh Kuruppu. gRPC: Up and Running[M]. O'Reilly Media, 2020.

## 许可证

本项目为本科毕业设计项目，仅供学习参考使用。