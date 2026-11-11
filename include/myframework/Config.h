#pragma once

#include <string>
#include <cstdint>

namespace myframework {

struct Config {
    int threadNum = 1;
    std::string logLevel = "INFO";

    bool enableHttp = true;
    uint16_t httpPort = 8080;
    std::string staticFileDir;
    int httpTimeoutSec = 30;

    bool enableRpc = false;
    uint16_t rpcPort = 9090;
    int rpcTimeoutMs = 5000;

    class Builder;
    static Builder build();
};

class Config::Builder {
public:
    Builder& threadNum(int n);
    Builder& logLevel(std::string l);
    Builder& httpPort(uint16_t p);
    Builder& enableHttp(bool v);
    Builder& staticFileDir(std::string d);
    Builder& httpTimeout(int s);
    Builder& enableRpc(bool v);
    Builder& rpcPort(uint16_t p);
    Builder& rpcTimeout(int ms);
    Config build();

private:
    Config conf_;
};

} // namespace myframework