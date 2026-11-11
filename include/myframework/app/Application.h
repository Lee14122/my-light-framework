#pragma once

#include <memory>
#include "EventLoop.h"
#include "myframework/Config.h"
#include "myframework/rpc/RpcServer.h"

namespace myframework {

/**
 * 框架统一启动入口（RPC-only 版本）。
 *
 * 读取配置，初始化 RPC 服务器，启动事件循环。
 *
 * 用法:
 *   myframework::Application app;
 *   app.init(Config::build().rpcPort(9090).threadNum(4).build());
 *   app.rpcServer().registerService("UserService", {...});
 *   app.run();
 */
class Application {
public:
    Application();
    ~Application();

    // 初始化（传入配置）
    void init(const Config& config);

    // 获取 RPC 服务器引用，用于注册服务
    RpcServer& rpcServer();

    // 启动（阻塞，进入事件循环）
    void run();

    // 停止
    void stop();

private:
    Config config_;
    EventLoop loop_;

    std::unique_ptr<RpcServer> rpcServer_;
};

} // namespace myframework