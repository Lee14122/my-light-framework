#include "myframework/Config.h"

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
    return conf_;
}

} // namespace myframework