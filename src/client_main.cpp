/**
 * RPC 客户端 Demo
 * 
 * 运行方式: ./rpc_client [host] [port]
 * 功能: 连接 RPC 服务器，发起多次调用，打印结果
 * 
 * 提示: muduo 内部日志输出到 stdout，若嫌干扰可重定向:
 *       ./rpc_client 2>/dev/null | grep -v '^\[INFO\]'
 */
#include "myframework/rpc/RpcClient.h"
#include "myframework/rpc/RpcCodec.h"
#include "myframework/Logger.h"
#include "EventLoop.h"
#include <iostream>
#include <string>
#include <memory>
#include <atomic>

using namespace myframework;

int main(int argc, char* argv[]) {
    std::string host = (argc > 1) ? argv[1] : "127.0.0.1";
    uint16_t  port  = (argc > 2) ? static_cast<uint16_t>(std::stoi(argv[2])) : 9090;

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════╗\n";
    std::cout << "║   My Light RPC Framework - Client Demo       ║\n";
    std::cout << "╚══════════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "[Client] Target: " << host << ":" << port << "\n";
    std::cout << "[Client] Connecting...\n\n";

    EventLoop loop;

    auto client = std::make_shared<RpcClient>(&loop);

    // 待处理调用计数器，全部完成后退
    auto pendingCount = std::make_shared<std::atomic_int>(0);

    auto onDone = [pendingCount, &loop]() {
        if (pendingCount->fetch_sub(1) == 1) {
            // 所有调用完成
            std::cout << "\n────────────────────────────────────────────────\n";
            std::cout << "[Client] All calls completed. Demo done!\n\n";
            loop.quit();
        }
    };

    // 连接就绪后自动发起调用
    client->setConnectedCallback([client, &loop, pendingCount, onDone]() {
        std::cout << "[Client] ✓ Connected!\n\n";
        std::cout << "────────────────────────────────────────────────\n";
        std::cout << "  Sending 3 RPC calls...\n";
        std::cout << "────────────────────────────────────────────────\n";

        // ======== 调用 1: EchoService.echo ========
        {
            pendingCount->fetch_add(1);
            std::cout << "\n[Call #1] EchoService.echo(\"Hello RPC!\")\n";
            client->asyncCall(
                "EchoService", "echo", "\"Hello RPC!\"",
                [onDone](const RpcValue& result) {
                    std::cout << "  ✅ SUCCESS: " << result << "\n";
                    onDone();
                },
                [onDone](int code, const std::string& msg) {
                    std::cout << "  ❌ ERROR [" << code << "]: " << msg << "\n";
                    onDone();
                });
        }

        // ======== 调用 2: CalcService.add ========
        {
            pendingCount->fetch_add(1);
            std::cout << "\n[Call #2] CalcService.add(\"test data\")\n";
            client->asyncCall(
                "CalcService", "add", "\"test data\"",
                [onDone](const RpcValue& result) {
                    std::cout << "  ✅ SUCCESS: " << result << "\n";
                    onDone();
                },
                [onDone](int code, const std::string& msg) {
                    std::cout << "  ❌ ERROR [" << code << "]: " << msg << "\n";
                    onDone();
                });
        }

        // ======== 调用 3: 不存在的服务（测试错误处理）=======
        {
            pendingCount->fetch_add(1);
            std::cout << "\n[Call #3] UnknownService.foo(\"should fail\")\n";
            client->asyncCall(
                "UnknownService", "foo", "\"should fail\"",
                [onDone](const RpcValue& result) {
                    std::cout << "  ✅ UNEXPECTED SUCCESS: " << result << "\n";
                    onDone();
                },
                [onDone](int code, const std::string& msg) {
                    std::cout << "  ❌ EXPECTED ERROR [" << code << "]: " << msg << "\n";
                    onDone();
                });
        }
    });

    // 发起连接
    client->connect(host, port);

    // 进入事件循环（阻塞至所有调用完成）
    loop.loop();

    return 0;
}