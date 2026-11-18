/**
 * 超时机制测试：
 * 1. 服务端 sleep 500ms，客户端 100ms 超时 → 应触发 -5 错误
 * 2. 超时后，服务端迟到响应到达，客户端应静默丢弃（不刷 WARN）
 */
#include "myframework/rpc/RpcClient.h"
#include "myframework/rpc/RpcServer.h"
#include "myframework/rpc/RpcCodec.h"
#include "myframework/Logger.h"
#include "myframework/rpc/RpcValue.h"
#include <muduo/net/EventLoopThread.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

using namespace myframework;

int main() {
    // ---- 服务端 ----
    // 注意：server 必须先于 serverLoopThread 声明，
    // 这样析构时 serverLoopThread 先 join（确保 server 在 loop 线程已销毁），
    // 然后 server 唯一指针析构才安全。
    std::unique_ptr<RpcServer> server;
    muduo::net::EventLoopThread serverLoopThread;
    EventLoop* serverLoop = serverLoopThread.startLoop();

    InetAddress addr(9091);
    server = std::make_unique<RpcServer>(serverLoop, addr, "TimeoutSrv");

    server->registerMethod("SlowService", "sleep", [](RpcContext& ctx) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        ctx.reply(RpcValue("done"));
    });

    // start() 必须在 loop 线程调用
    serverLoop->runInLoop([&] { server->start(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // ---- 客户端 ----
    EventLoop clientLoop;
    auto client = std::make_shared<RpcClient>(&clientLoop);

    client->setConnectedCallback([client, &clientLoop]() {
        std::cout << "\n[Test] Sending slow call with 100ms timeout...\n";
        int64_t startMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        client->asyncCall("SlowService", "sleep", {},
            [&](const RpcValue&) {
                std::cout << "❌ UNEXPECTED SUCCESS (should have timed out)\n";
                clientLoop.quit();
            },
            [&](int code, const std::string& msg) {
                int64_t elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count() - startMs;
                std::cout << "✅ ERROR received: code=" << code
                          << " msg=" << msg
                          << " elapsed=" << elapsedMs << "ms\n";
                if (code != -5) {
                    std::cout << "❌ Expected code -5 (timeout), got " << code << "\n";
                } else if (elapsedMs < 80 || elapsedMs > 300) {
                    std::cout << "❌ Timeout not in expected range [80, 300]ms\n";
                } else {
                    std::cout << "✅ Timeout fired at correct time\n";
                }
                // 等 1s 看迟到响应是否会打印 WARN
                std::this_thread::sleep_for(std::chrono::seconds(1));
                clientLoop.quit();
            },
            100  // 100ms timeout
        );
    });

    client->connect("127.0.0.1", 9091);
    clientLoop.loop();

    // 清理：在 loop 线程中销毁 server（TcpServer::~TcpServer 要求 loop 线程）
    // EventLoopThread 析构时会 quit + join，所以在析构前把 server.reset() 排入队列，
    // join 期间 loop 线程会处理该 functor。
    serverLoop->runInLoop([&] { server.reset(); });

    // serverLoopThread 析构 → quit() → wakeup → doPendingFunctors (server.reset) → loop 退出 → join
    // server 此时已是空指针，后续唯一指针析构安全无副作用

    std::cout << "\n✅ Timeout test done\n";
    return 0;
}