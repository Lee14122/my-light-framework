/**
 * RPC 服务端 Demo
 * 
 * 运行方式: ./rpc_server
 * 功能: 注册 EchoService，等待客户端调用
 */
#include "myframework/app/Application.h"
#include "myframework/Config.h"
#include "myframework/rpc/RpcServer.h"
#include "myframework/Logger.h"
#include <iostream>
#include <signal.h>

using namespace myframework;

// 全局指针，用于信号处理
Application* g_app = nullptr;

void signalHandler(int) {
    std::cout << "\n[Server] Received shutdown signal, stopping..." << std::endl;
    if (g_app) g_app->stop();
}

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════╗\n";
    std::cout << "║   My Light RPC Framework - Server Demo       ║\n";
    std::cout << "╚══════════════════════════════════════════════╝\n";
    std::cout << "\n";

    // 1. 初始化
    Application app;
    g_app = &app;
    Config config = Config::build()
        .enableRpc(true)
        .rpcPort(9090)
        .threadNum(1)    // 1 个 IO 线程
        .build();
    app.init(config);

    // 2. 注册服务
    auto& server = app.rpcServer();

    // ---- EchoService ----
    server.registerMethod("EchoService", "echo",
        [](RpcContext& ctx) {
            std::cout << "\n┌─ [EchoService.echo] ──────────────────────────┐\n";
            std::cout << "│  seqId  = " << ctx.seqId << "\n";
            std::cout << "│  args   = " << ctx.args << "\n";
            std::cout << "│  action = echoing back...\n";
            std::cout << "└──────────────────────────────────────────────┘\n";
            ctx.reply(ctx.args);
        });

    // ---- CalcService ----
    server.registerMethod("CalcService", "add",
        [](RpcContext& ctx) {
            std::cout << "\n┌─ [CalcService.add] ───────────────────────────┐\n";
            std::cout << "│  args   = " << ctx.args << "\n";
            std::cout << "│  result = " << ctx.args << " (echo for demo)\n";
            std::cout << "└──────────────────────────────────────────────┘\n";
            ctx.reply(ctx.args);
        });

    std::cout << "\n[Server] Registered services:\n";
    std::cout << "  • EchoService.echo   — 回显服务\n";
    std::cout << "  • CalcService.add    — 计算服务 (示例)\n";
    std::cout << "\n[Server] Listening on 0.0.0.0:9090 ...\n";
    std::cout << "[Server] Press Ctrl+C to stop\n";
    std::cout << "────────────────────────────────────────────────\n\n";

    // 3. 启动
    app.run();

    std::cout << "\n[Server] Shutdown complete.\n";
    return 0;
}