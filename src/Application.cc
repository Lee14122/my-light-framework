#include "myframework/app/Application.h"
#include "myframework/Logger.h"
#include "InetAddress.h"

namespace myframework {

Application::Application() = default;
Application::~Application() = default;

void Application::init(const Config& config) {
    config_ = config;

    if (config_.enableRpc) {
        InetAddress addr(config_.rpcPort);
        rpcServer_ = std::make_unique<RpcServer>(&loop_, addr, "MyRpcServer");
        rpcServer_->setThreadNum(config_.threadNum);
        rpcServer_->setHeartbeatInterval(30);
        MFLOG_INFO("App", "RPC server initialized on port %d", config_.rpcPort);
    }
}

RpcServer& Application::rpcServer() {
    return *rpcServer_;
}

void Application::run() {
    if (rpcServer_) {
        rpcServer_->start();
    }
    MFLOG_INFO("App", "Application running, entering event loop...");
    loop_.loop();
}

void Application::stop() {
    MFLOG_INFO("App", "Application stopping...");
    loop_.quit();
}

} // namespace myframework