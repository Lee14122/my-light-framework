# My Light RPC Framework — 学习文档

> 本文档带你从零理解这个基于 muduo-core 构建的高性能 RPC 框架。
> 配合源代码阅读效果最佳：每个章节标注了对应的源文件。

---

## 目录

1. [项目概览](#1-项目概览)
2. [架构全景图](#2-架构全景图)
3. [分层讲解（自底向上）](#3-分层讲解自底向上)
   - [3.1 协议层：RpcMessage](#31-协议层rpcmessage)
   - [3.2 序列化层：Serializer](#32-序列化层serializer)
   - [3.3 编解码层：RpcCodec](#33-编解码层rpccodec)
   - [3.4 传输层：TcpServerAdapter](#34-传输层tcpserveradapter)
   - [3.5 服务层：RpcServer](#35-服务层rpcserver)
   - [3.6 客户端层：RpcClient](#36-客户端层rpcclient)
   - [3.7 应用层：Application](#37-应用层application)
4. [一次 RPC 调用的完整旅程](#4-一次-rpc-调用的完整旅程)
5. [关键设计决策](#5-关键设计决策)
6. [如何扩展](#6-如何扩展)

---

## 1. 项目概览

### 这是什么？

一个**类 gRPC 风格的 RPC（Remote Procedure Call）框架**。它让你能像调用本地函数一样调用远程服务器上的方法。

```
你的代码                    框架自动完成
══════════                  ══════════
client->asyncCall(          ① 序列化参数 → JSON
  "EchoService",            ② 打包成二进制帧
  "echo",                   ③ TCP 发送
  "Hello"                   ④ 服务端解析、分发
);                          ⑤ 执行 handler
                            ⑥ 序列化返回值
                            ⑦ 打包响应帧
                            ⑧ TCP 回传
                            ⑨ 客户端匹配、回调
```

### 核心能力

| 能力 | 说明 | 对应代码 |
|------|------|---------|
| **自定义二进制协议** | 28字节定长帧头 + 可变长字段 | `RpcMessage.h` |
| **可插拔序列化** | 默认 JSON（调试用），可替换为 Protobuf | `Serializer.h` |
| **TCP 粘包/拆包** | 从字节流中切出完整帧 | `RpcCodec::onData()` |
| **服务注册与分发** | serviceName → methodName → handler | `RpcServer::dispatch()` |
| **连接池** | 客户端多连接并发，round-robin 调度 | `RpcClient::pickChannel()` |
| **异步/同步调用** | asyncCall + syncCall | `RpcClient` |
| **请求-响应匹配** | seqId 关联请求和响应 | `pendingCalls_` map |
| **心跳保活** | 服务端响应心跳 | `RpcServer::handleHeartbeat()` |

### 依赖关系

```
本项目 myframework
    │
    └── 依赖 muduo-core（教学精简版）
         ├── EventLoop    — 事件循环（epoll 封装）
         ├── TcpServer    — TCP 服务器
         ├── TcpConnection— 单条 TCP 连接
         ├── Buffer       — 字节缓冲区
         ├── Channel      — fd + 事件回调
         └── ...          — Socket, InetAddress, Poller 等
```

**重要限制**：muduo-core 中所有类都在**全局命名空间**，无 `muduo::` 前缀。我的框架统一使用 `myframework::` 命名空间。

---

## 2. 架构全景图

```
┌─────────────────────────────────────────────────────┐
│                   Application                       │  ← 启动入口
│         (初始化配置、启动服务器、进入事件循环)          │
└──────────────┬──────────────┬───────────────────────┘
               │              │
      ┌────────▼────────┐  ┌──▼───────────────────────┐
      │   RpcServer     │  │      RpcClient           │
      │                 │  │                           │
      │ • 服务注册表     │  │ • 连接池 (RpcChannel[])   │
      │ • 请求分发       │  │ • 待处理调用表 (seqId→cb) │
      │ • 心跳处理       │  │ • Round-Robin 调度        │
      └───────┬─────────┘  └──────┬────────────────────┘
              │                   │
   ┌──────────▼──────────┐  ┌─────▼──────────────────┐
   │  TcpServerAdapter   │  │   RpcCodec (per conn)  │
   │  (封装 muduo        │  │   • encode / decode    │
   │   TcpServer)        │  │   • onData (拆帧)      │
   └──────────┬──────────┘  └─────┬──────────────────┘
              │                   │
   ┌──────────▼───────────────────▼──────────────────┐
   │              Serializer (可插拔)                  │
   │  JsonSerializer (默认) | BinarySerializer (stub) │
   └──────────────────────┬──────────────────────────┘
                          │
   ┌──────────────────────▼──────────────────────────┐
   │              RpcMessage (协议帧)                  │
   │  28字节帧头: magic|totalLen|type|seqId|...       │
   │  + serviceName + methodName + meta + payload     │
   └─────────────────────────────────────────────────┘
                          │
   ┌──────────────────────▼──────────────────────────┐
   │           muduo-core (网络传输层)                 │
   │  EventLoop → Poller(epoll) → Channel → TcpConn   │
   └─────────────────────────────────────────────────┘
```

**分层原则**：上层只依赖直接下层，不越级调用。

---

## 3. 分层讲解（自底向上）

### 3.1 协议层：RpcMessage

> **文件**: `include/myframework/rpc/RpcMessage.h`, `src/RpcMessage.cc`

这是整个框架最底层的数据结构——定义「一帧数据长什么样」。

#### 二进制帧格式（28 字节定长头）

```
字节偏移  0        4        8        10   11   12       16       18       20       22       26   28
        ┌────────┬────────┬────────┬────┬────┬────────┬────────┬────────┬────────┬────────┬──────┐
        │ magic  │totalLen│hdrLen  │type│comp│ seqId  │svcLen  │mthLen  │metaLen │payLen  │ ...  │
        │ 4B     │ 4B     │ 2B     │1B  │1B  │ 4B     │ 2B     │ 2B     │ 2B    │ 4B     │      │
        └────────┴────────┴────────┴────┴────┴────────┴────────┴────────┴────────┴────────┴──────┘
        |←—————————————— 28 字节固定帧头 ——————————————→|←—— 可变长数据（长度由帧头指定） ——→|
```

#### 关键字段含义

| 字段 | 大小 | 含义 | 例子 |
|------|------|------|------|
| `magic` | 4B | 魔数 `0xABCD1234`，快速识别协议帧 | 收到垃圾数据时第一道防线 |
| `totalLen` | 4B | **整帧总长度**（含帧头），用于拆帧 | `totalLen = 28 + svcLen + mthLen + metaLen + payLen` |
| `type` | 1B | 消息类型 | 1=REQUEST, 2=RESPONSE, 3=ERROR, 4=HEARTBEAT |
| `seqId` | 4B | **序列号**，关联请求与响应 | 客户端发 seqId=5，服务端回复带 seqId=5 |
| `serviceLen` | 2B | 服务名字节数 | `"EchoService"` = 12 |
| `payload` | 变长 | 序列化后的业务数据（JSON 字符串） | `{"jsonrpc":"2.0","method":"EchoService.echo",...}` |

#### 关键方法

```cpp
void computeLengths();   // 根据可变字段内容，自动计算所有长度字段
bool validate() const;   // 校验 magic 和 totalLen 一致性
```

---

### 3.2 序列化层：Serializer

> **文件**: `include/myframework/rpc/Serializer.h`, `src/Serializer.cc`

**设计意图**：将"业务数据"和"网络字节"之间的转换抽象为接口，方便未来替换。

#### 接口定义

```cpp
class Serializer {
public:
    virtual std::string buildRequest(service, method, args) = 0;  // 构建请求 payload
    virtual std::string buildResponse(result) = 0;                // 构建响应 payload
    virtual std::string buildError(code, message) = 0;            // 构建错误 payload
    virtual std::pair<bool, RpcValue> parseResponse(payload) = 0; // 解析响应
};
```

#### JsonSerializer（默认实现）

手写 JSON 字符串拼接，零外部依赖。

```
请求 JSON 格式:
{"jsonrpc":"2.0","method":"EchoService.echo","params":["Hello RPC!"],"id":0}

响应 JSON 格式:
{"jsonrpc":"2.0","result":{"...原样返回..."}}

错误 JSON 格式:
{"jsonrpc":"2.0","error":{"code":-1,"message":"Service not found"}}
```

**如何替换为 Protobuf**：实现一个新的 `ProtobufSerializer : public Serializer`，重写 4 个虚函数即可。

#### BinarySerializer（stub 占位）

当前为透传实现，`serialize()` / `deserialize()` 原样返回。留给未来追求极致性能时替换。

---

### 3.3 编解码层：RpcCodec

> **文件**: `include/myframework/rpc/RpcCodec.h`, `src/RpcCodec.cc`

这是框架中最核心的组件之一，负责**TCP 字节流 ↔ RpcMessage 对象**之间的双向转换。

#### 两大职责

```
职责 1: 编码 (encode)      RpcMessage 对象  ──→  网络字节流 (大端序)
职责 2: 解码 + 拆帧 (onData)  TCP 字节流     ──→  完整 RpcMessage 对象
```

#### encode() — 编码流程

```cpp
std::string RpcCodec::encode(const RpcMessage& msg) {
    // 1. 计算各字段长度
    msg.computeLengths();

    // 2. 分配内存
    std::string result;
    result.resize(msg.totalLen);

    // 3. 写入帧头（28 字节），每个多字节字段转大端序
    memcpy(p + 0,  &magic_be,    4);   // htonl(magic)
    memcpy(p + 4,  &totalLen_be, 4);   // htonl(totalLen)
    // ... 逐个写入 ...

    // 4. 写入可变长数据
    memcpy(p + 28, serviceName.data(), serviceLen);
    memcpy(p + 28 + serviceLen, methodName.data(), methodLen);
    // ...
}
```

**为什么用大端序？** 网络字节序 = 大端序，`htonl()` = host-to-network-long，确保不同 CPU 架构（x86 小端、PowerPC 大端）互操作。

#### onData() — 拆帧流程（解决粘包/拆包）

TCP 是**字节流**，不保留消息边界。一次 `read()` 可能收到：
- 半帧（拆包）：需要缓存，等后续数据
- 多帧（粘包）：需要循环切分
- 刚好一帧：直接处理

```cpp
void RpcCodec::onData(Buffer* buf) {
    while (buf->readableBytes() >= HEADER_SIZE) {   // 够读帧头吗？
        // 1. peek 帧头中的 totalLen（不消费数据）
        uint32_t totalLen = peekTotalLen(buf);

        // 2. 检查长度合法性
        if (totalLen < 28 || totalLen > 64MB) → 丢弃

        // 3. 数据够一帧吗？
        if (buf->readableBytes() < totalLen) → return; // 等更多数据

        // 4. 提取完整帧
        string frameData(buf->peek(), totalLen);
        buf->retrieve(totalLen);         // 消费掉

        // 5. 解码 → 回调上层
        RpcMessage msg;
        if (decode(frameData, msg))
            messageCallback_(msg);       // → RpcServer::onRpcMessage
    }
}
```

```
图解粘包/拆包处理:

TCP 字节流: [帧A 的前半][帧A 的后半 + 帧B 完整][帧C 的前半]
                ↓              ↓              ↓
             累积到         切出帧A +      等待更多
             pendingBuf_    切出帧B         数据...
```

#### 每个连接一个 Codec

```cpp
// RpcServer 中，每来一个新连接就创建一个 RpcCodec
void RpcServer::onConnection(const TcpConnectionPtr& conn) {
    auto codec = std::make_shared<RpcCodec>();
    codecs_[conn->name()] = codec;  // 用连接名索引
}
```

---

### 3.4 传输层：TcpServerAdapter

> **文件**: `include/myframework/net/TcpServerAdapter.h`, `src/TcpServerAdapter.cc`

**设计意图**：隔离层（Adapter 模式），将 muduo 的 `TcpServer` 包装起来，对外暴露更简洁的接口。

```
RpcServer                    TcpServerAdapter              muduo TcpServer
    │                              │                            │
    │ setConnectionCallback(fn)    │                            │
    │─────────────────────────────>│                            │
    │                              │ setConnectionCallback()    │
    │                              │───────────────────────────>│
    │                              │                            │
    │                              │    onConnection回调         │
    │                              │<───────────────────────────│
    │    connectionCallback_(conn) │                            │
    │<─────────────────────────────│                            │
```

**关键设计**：`TcpServerAdapter` 的内部 `onConnection` / `onMessage` 先处理日志，再转发给上层回调。这样将来如果要加连接限流、IP 黑名单等，只需改这一层。

---

### 3.5 服务层：RpcServer

> **文件**: `include/myframework/rpc/RpcServer.h`, `src/RpcServer.cc`

#### 服务注册表

```cpp
// 数据结构: 两级 map
// serviceName → (methodName → handler)
unordered_map<string, unordered_map<string, MethodHandler>> services_;

// 注册:
server.registerMethod("EchoService", "echo", handler);
//  → services_["EchoService"]["echo"] = handler;
```

#### 请求分发流程（dispatch）

```
收到完整 RpcMessage
    │
    ├─ type == REQUEST  ──→ dispatch()
    │                          │
    │                   ┌──────▼──────┐
    │                   │ 查找 service │  ← services_.find(msg.serviceName)
    │                   └──────┬──────┘
    │                    找不到  │  找到
    │                   sendError│   │
    │                            │   │
    │                   ┌────────▼──────┐
    │                   │ 查找 method   │  ← services_[svc].find(msg.methodName)
    │                   └──────┬──────┘
    │                    找不到  │  找到
    │                   sendError│   │
    │                            │   │
    │                   ┌────────▼──────────┐
    │                   │ 构造 RpcContext   │
    │                   │ ctx.args  = payload│
    │                   │ ctx.reply = lambda │  ← 闭包捕获 conn, seqId
    │                   └────────┬──────────┘
    │                            │
    │                   ┌────────▼──────┐
    │                   │ 调用 handler  │  ← handler(ctx)
    │                   └───────────────┘
    │
    ├─ type == HEARTBEAT ──→ handleHeartbeat() → sendResponse(pong)
    │
    └─ type == RESPONSE/ERROR → (服务端不应收到响应，忽略)
```

#### RpcContext — handler 的"工具箱"

```cpp
struct RpcContext {
    uint32_t    seqId;       // 请求序列号
    std::string service;     // 服务名
    std::string method;      // 方法名
    RpcValue    args;        // 请求参数
    RpcValue    meta;        // 透传元数据（如 trace-id）

    // 在 handler 中调用这两个函数来回复客户端：
    std::function<void(const RpcValue&)> reply;             // 正常响应
    std::function<void(int, const std::string&)> replyError; // 错误响应
};
```

**闭包魔法**：`reply` 和 `replyError` 是 lambda 闭包，捕获了 `TcpConnectionPtr`（知道发给谁）和 `seqId`（知道对应哪个请求），handler 只需调用 `ctx.reply(result)` 即可。

---

### 3.6 客户端层：RpcClient

> **文件**: `include/myframework/rpc/RpcClient.h`, `src/RpcClient.cc`

这是最复杂的组件，因为它需要处理非阻塞连接、连接池调度、请求-响应匹配。

#### 连接建立（非阻塞 connect）

muduo-core **没有** `TcpClient` 类，所以客户端的连接需要手动实现：

```
connectPool("127.0.0.1", 9090, N)
    │
    ├─ ① socket() + fcntl(O_NONBLOCK)         创建非阻塞 socket
    ├─ ② connect() → 返回 EINPROGRESS           TCP 三次握手开始（异步）
    ├─ ③ Channel(loop, sockfd)                 创建 epoll 监听
    │     ch->enableWriting()                   监听 EPOLLOUT 事件
    │     ch->setWriteCallback(onConnectWrite)
    │
    └─ 等事件循环运行...
              │
         EPOLLOUT 触发（连接完成）
              │
    ┌─────────▼──────────────┐
    │ onConnectWrite(ctx)    │
    │ ① getsockopt(SO_ERROR) │  检查连接是否成功
    │ ② 移除旧 Channel        │  避免与 TcpConnection 的 Channel 冲突
    │ ③ 创建 TcpConnection   │  用成功的 fd 包装
    │ ④ 设置 RpcCodec        │  每连接一个编解码器
    │ ⑤ connectEstablished() │  启用读写事件
    │ ⑥ 加入 channels_[]     │  加入连接池
    └────────────────────────┘
```

#### 连接池 + Round-Robin 调度

```cpp
RpcChannel* pickChannel() {
    // 从上次位置开始，找第一个就绪的连接
    for (i = 0; i < channels_.size(); i++) {
        idx = (nextChannelIndex_ + i) % channels_.size();
        if (channels_[idx]->ready) {
            nextChannelIndex_ = idx + 1;    // 下次从下一个开始
            return channels_[idx];
        }
    }
}
```

#### 异步调用流程

```cpp
void asyncCall("EchoService", "echo", args, onSuccess, onError) {
    // 1. 选一个可用的连接
    RpcChannel* ch = pickChannel();

    // 2. 分配序列号
    uint32_t seqId = nextSeqId_++;       // 原子递增，线程安全

    // 3. 注册回调（等响应回来时匹配）
    pendingCalls_[seqId] = {onSuccess, onError};

    // 4. 序列化 + 编码 + 发送
    string payload   = serializer_->buildRequest(service, method, args);
    RpcMessage req   = RpcCodec::makeRequest(seqId, service, method, payload);
    string    encoded = RpcCodec::encode(req);
    ch->conn->send(encoded);
}
```

#### 响应匹配

```cpp
void onMessage(RpcChannel* ch, const RpcMessage& msg) {
    // 收到响应帧，按 seqId 查找对应回调
    auto it = pendingCalls_.find(msg.seqId);
    if (it != pendingCalls_.end()) {
        auto call = std::move(it->second);   // 取出回调
        pendingCalls_.erase(it);             // 清理

        if (msg.type == RESPONSE)
            call.onSuccess(parseResponse(msg.payload));
        else
            call.onError(parseError(msg.payload));
    }
}
```

#### syncCall — 基于 asyncCall 实现

```cpp
RpcValue syncCall(service, method, args) {
    std::promise<pair<bool, RpcValue>> promise;
    auto future = promise.get_future();

    asyncCall(service, method, args,
        [&](result) { promise.set_value({true, result}); },   // 成功 → 唤醒
        [&](code,msg){ promise.set_value({false, ...});   }    // 失败 → 唤醒
    );

    return future.get();   // 阻塞等待，直到回调设置值
}
```

**注意**：`syncCall` 会阻塞调用线程，不要在 EventLoop 线程中调用（会死锁）。

---

### 3.7 应用层：Application

> **文件**: `include/myframework/app/Application.h`, `src/Application.cc`

最薄的封装层，负责：

```cpp
Application app;

// 1. 加载配置
app.init(Config::build()
    .rpcPort(9090)
    .threadNum(4)
    .build());

// 2. 注册服务
app.rpcServer().registerMethod("Svc", "Mtd", handler);

// 3. 启动（阻塞，进入事件循环）
app.run();
```

---

## 4. 一次 RPC 调用的完整旅程

以下追踪 `client.asyncCall("EchoService", "echo", "\"Hello\"")` 从发起到收到响应的全过程：

```
┌─────────────────────────────────────────────────────────────────────────┐
│ 步骤 1: 客户端发起调用                                                     │
│                                                                         │
│ asyncCall("EchoService", "echo", "\"Hello\"", onSuccess, onError)      │
│   │                                                                     │
│   ├─① pickChannel() → 选 channel[0]                                     │
│   ├─② seqId = nextSeqId_++  →  得到 seqId=1                             │
│   ├─③ pendingCalls_[1] = {onSuccess, onError}                           │
│   ├─④ serializer_->buildRequest("EchoService", "echo", ["\"Hello\""])  │
│   │     → {"jsonrpc":"2.0","method":"EchoService.echo",                │
│   │        "params":["\"Hello\""],"id":0}                              │
│   ├─⑤ RpcCodec::makeRequest(1, "EchoService", "echo", payload)         │
│   │     → RpcMessage{type=REQUEST, seqId=1, serviceName="EchoService", │
│   │                   methodName="echo", payload=<JSON>}                │
│   ├─⑥ RpcCodec::encode(msg) → 网络字节流                                 │
│   │     [AB CD 12 34] [totalLen] ... [EchoService][echo][{JSON}]       │
│   └─⑦ channel->conn->send(bytes) → muduo → TCP                         │
│                                                                         │
├─────────────────────────────────────────────────────────────────────────┤
│ 步骤 2: TCP 传输                                                         │
│                                                                         │
│   客户端 ──── TCP segments ────→ 服务端                                   │
│                                                                         │
├─────────────────────────────────────────────────────────────────────────┤
│ 步骤 3: 服务端接收并拆帧                                                    │
│                                                                         │
│ muduo epoll 通知 → TcpServerAdapter::onMessage(buf)                    │
│   └─⑧ RpcServer::onMessage(conn, buf)                                   │
│       └─⑨ codecs_[conn->name()]->onData(buf)   ← 找到对应连接的 codec    │
│           └─⑩ 从 buf 中切出完整帧 → decode → 得到 RpcMessage              │
│               └─⑪ messageCallback_(msg) → RpcServer::onRpcMessage()     │
│                                                                         │
├─────────────────────────────────────────────────────────────────────────┤
│ 步骤 4: 服务端分发并执行                                                    │
│                                                                         │
│ onRpcMessage(conn, msg)                                                 │
│   └─⑫ dispatch(conn, msg)                                               │
│       ├─ services_.find("EchoService") → 找到                            │
│       ├─ services_["EchoService"].find("echo") → 找到                    │
│       ├─ 构造 RpcContext{seqId=1, args=<JSON>, ...}                     │
│       ├─ ctx.reply = [conn, seqId](result) {                            │
│       │     serializer_->buildResponse(result)                          │
│       │     → RpcCodec::makeResponse(1, payload)                        │
│       │     → RpcCodec::encode → conn->send()                           │
│       │   }                                                             │
│       └─⑬ handler(ctx)                                                 │
│           └─ ctx.reply("\"Hello\"")   ← 调用闭包发送响应                  │
│                                                                         │
├─────────────────────────────────────────────────────────────────────────┤
│ 步骤 5: TCP 回传                                                         │
│                                                                         │
│   服务端 ──── TCP segments ────→ 客户端                                   │
│                                                                         │
├─────────────────────────────────────────────────────────────────────────┤
│ 步骤 6: 客户端匹配响应                                                     │
│                                                                         │
│ muduo epoll 通知 → TcpConnection::handleRead                            │
│   └─⑭ RpcCodec::onData(buf)                                             │
│       └─⑮ decode → 得到 RpcMessage{type=RESPONSE, seqId=1, payload=...}│
│           └─⑯ messageCallback_(msg) → RpcClient::onMessage()            │
│               ├─ pendingCalls_.find(1) → 找到                            │
│               ├─ call = pendingCalls_[1]; pendingCalls_.erase(1)        │
│               ├─ serializer_->parseResponse(payload) → {true, result}   │
│               └─⑰ call.onSuccess(result)                                 │
│                   └─ 用户回调：打印 "SUCCESS!"                             │
└─────────────────────────────────────────────────────────────────────────┘
```

**17 个步骤**，从 `asyncCall` 到 `onSuccess`，全程自动完成。

---

## 5. 关键设计决策

### 5.1 为什么手写 JSON 而不是引入 nlohmann/json？

**v1 版本的目标是"零外部依赖"**。手写 JSON 拼接虽然简陋，但：
- 不需要额外的 CMake 配置
- 编译更快
- 后续替换为 Protobuf 时，只需实现一个新的 Serializer 子类

### 5.2 为什么 muduo-core 没有 TcpClient？

muduo-core 是**教学精简版**，只保留了服务端的核心类（TcpServer、Acceptor）。本项目手动实现了客户端的非阻塞 connect：
- `socket()` + `connect()` (EINPROGRESS) 
- `Channel` 监听 `EPOLLOUT`
- `getsockopt(SO_ERROR)` 判断连接成功
- 然后包装为 `TcpConnection`

### 5.3 为什么 seqId 用 atomic 递增？

```cpp
std::atomic_uint32_t nextSeqId_{1};
uint32_t seqId = nextSeqId_.fetch_add(1);
```

虽然当前所有调用都在 EventLoop 线程（单线程），但设计上为**多线程并发调用**留了余地——将来用户可能从多个业务线程同时调用 `asyncCall`。

### 5.4 为什么每个连接一个 RpcCodec？

TCP 连接是独立的字节流，两个连接的帧边界互不干扰。如果共用 codec，`pendingBuffer_` 中的数据会串台（连接 A 的半帧被当成连接 B 数据的开头）。

### 5.5 syncCall 为什么可能死锁？

```cpp
RpcValue syncCall(...) {
    auto future = promise.get_future();
    asyncCall(..., [&] { promise.set_value(...); });
    return future.get();  // 阻塞！
}
```

如果在 EventLoop 线程调用 `syncCall`：
1. `asyncCall` 发送请求（OK）
2. `future.get()` 阻塞当前线程
3. 但**响应需要 EventLoop 线程来接收和处理**
4. EventLoop 线程被 `future.get()` 阻塞了 → **死锁**

**正确用法**：在独立线程中调用 `syncCall`，或使用 `asyncCall`。

---

## 6. 如何扩展

### 6.1 添加新服务

```cpp
// 在 server_main.cpp 中：
server.registerMethod("UserService", "GetUser",
    [](RpcContext& ctx) {
        // ctx.args 是请求参数
        // ctx.reply(result) 返回结果
        std::string result = R"({"id":1,"name":"Alice"})";
        ctx.reply(result);
    });
```

### 6.2 替换为 Protobuf 序列化

```cpp
class ProtobufSerializer : public Serializer {
public:
    std::string buildRequest(service, method, args) override {
        // 1. 将 args 反序列化为 Protobuf message
        // 2. 序列化为二进制
        // 3. 返回
    }
    // ... 其他方法 ...
};

// 使用:
server.setSerializer(std::make_unique<ProtobufSerializer>());
client.setSerializer(std::make_unique<ProtobufSerializer>());
```

### 6.3 实现超时控制

当前 `timeoutMs` 参数已预留但未生效。需要：

1. 在 muduo-core 中实现 TimerQueue（或引入定时器库）
2. 在 `asyncCall` 中启动定时器
3. 超时时自动调用 `onError` 并清理 `pendingCalls_`

### 6.4 添加服务发现

```cpp
class RpcClient {
    // 新增：从注册中心获取服务地址
    bool connectFromRegistry(const string& registryAddr,
                              const string& serviceName);
};
```

### 6.5 代码阅读顺序建议

如果你想深入理解这个框架，建议按以下顺序阅读：

```
1. include/myframework/rpc/RpcMessage.h    ← 协议帧格式（数据结构）
2. include/myframework/rpc/Serializer.h    ← 序列化接口
3. src/Serializer.cc                       ← JSON 序列化实现
4. src/RpcMessage.cc                       ← 帧校验
5. include/myframework/rpc/RpcCodec.h      ← 编解码接口
6. src/RpcCodec.cc                         ← 编解码 + 拆帧实现
7. src/TcpServerAdapter.cc                 ← muduo 封装（短）
8. include/myframework/rpc/RpcServer.h     ← 服务端接口
9. src/RpcServer.cc                        ← 服务注册 + 分发实现
10. include/myframework/rpc/RpcClient.h     ← 客户端接口
11. src/RpcClient.cc                        ← 连接池 + 调用实现
12. src/server_main.cpp / client_main.cpp   ← Demo 入口
```