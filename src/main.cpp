#include "myframework/app/Application.h"
#include "myframework/Config.h"
#include "myframework/rpc/RpcServer.h"
#include "myframework/rpc/RpcClient.h"
#include "myframework/Logger.h"
#include "EventLoop.h"
#include <iostream>
#include <string>
#include <memory>

using namespace myframework;

int main() {
    std::cout << "===== My Light RPC Framework Demo =====" << std::endl;

    // 1. 初始化应用 + RPC 服务器
    Application app;
    Config config = Config::build()
        .enableRpc(true)
        .rpcPort(9090)
        .threadNum(0)       // 单线程模式（方便调试）
        .build();

    app.init(config);

    // 2. 注册 EchoService.echo 方法
    app.rpcServer().registerMethod("EchoService", "echo",
        [](RpcContext& ctx) {
            std::cout << "[Server] EchoService.echo called, args="
                      << ctx.args << std::endl;
            // 将收到的参数原样返回
            ctx.reply(ctx.args);
        });

    std::cout << "[Main] Registered EchoService.echo" << std::endl;

    // 3. 先启动服务器（使其开始监听）
    app.rpcServer().start();
    std::cout << "[Main] RPC Server started on port 9090" << std::endl;

    // 4. 创建客户端
    auto client = std::make_shared<RpcClient>(app.rpcServer().getLoop());

    // 当连接就绪时，发起 async RPC 调用
    client->setConnectedCallback([client, &app]() {
        std::cout << "[Client] Connection ready, sending async RPC call..." << std::endl;

        client->asyncCall(
            "EchoService",
            "echo",
            "\"Hello, RPC World!\"",
            // 成功回调
            [&app](const RpcValue& result) {
                std::cout << "[Client] SUCCESS! Result: " << result << std::endl;
                std::cout << "===== Demo completed successfully! =====" << std::endl;
                app.stop();
            },
            // 错误回调
            [&app](int code, const std::string& msg) {
                std::cout << "[Client] ERROR: code=" << code
                          << ", message=" << msg << std::endl;
                app.stop();
            },
            5000
        );
    });

    // 5. 服务器已启动，现在连接客户端
    client->connect("127.0.0.1", 9090);
    std::cout << "[Main] Client connect initiated, entering event loop..." << std::endl;

    // 6. 进入事件循环（阻塞直到 app.stop() 被调用）
    app.run();

    std::cout << "[Main] Application exited." << std::endl;
    return 0;
}