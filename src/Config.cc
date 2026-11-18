#include "myframework/Config.h"
#include <stdexcept>

namespace myframework {

Config::Builder Config::build() {
    return Builder();
}

Config::Builder& Config::Builder::threadNum(int n)  { conf_.threadNum = n; return *this; }
Config::Builder& Config::Builder::logLevel(std::string l) { conf_.logLevel = std::move(l); return *this; }
Config::Builder& Config::Builder::httpPort(uint16_t p) { conf_.httpPort = p; return *this; }
Config::Builder& Config::Builder::enableHttp(bool v) { conf_.enableHttp = v; return *this; }
Config::Builder& Config::Builder::staticFileDir(std::string d) { conf_.staticFileDir = std::move(d); return *this; }
Config::Builder& Config::Builder::httpTimeout(int s) { conf_.httpTimeoutSec = s; return *this; }
Config::Builder& Config::Builder::enableRpc(bool v) { conf_.enableRpc = v; return *this; }
Config::Builder& Config::Builder::rpcPort(uint16_t p) { conf_.rpcPort = p; return *this; }
Config::Builder& Config::Builder::rpcTimeout(int ms) { conf_.rpcTimeoutMs = ms; return *this; }

Config Config::Builder::build() {
    // --- 校验 ---
    if (conf_.threadNum < 0) {
        throw std::invalid_argument("Config: threadNum must be >= 0, got " +
                                    std::to_string(conf_.threadNum));
    }

    if (conf_.enableHttp && conf_.httpPort == 0) {
        throw std::invalid_argument("Config: httpPort must be 1-65535 when enableHttp=true");
    }
    if (conf_.enableRpc && conf_.rpcPort == 0) {
        throw std::invalid_argument("Config: rpcPort must be 1-65535 when enableRpc=true");
    }

    if (conf_.httpTimeoutSec < 0) {
        throw std::invalid_argument("Config: httpTimeout must be >= 0");
    }
    if (conf_.rpcTimeoutMs < 0) {
        throw std::invalid_argument("Config: rpcTimeoutMs must be >= 0");
    }

    return conf_;
}

} // namespace myframework