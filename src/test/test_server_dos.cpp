#include "myframework/rpc/RpcMessage.h"
#include "myframework/rpc/RpcCodec.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

using namespace myframework;

int main() {
    // 1. 构造 600 层嵌套的恶意 payload
    std::string payload;
    for (int i = 0; i < 600; ++i) payload += "[";
    for (int i = 0; i < 600; ++i) payload += "]";

    // 2. 用框架自带的 RpcCodec 构造一个合法的 RpcMessage
    //    就像正常客户端一样，只不过 payload 是炸弹
    RpcMessage attack = RpcCodec::makeRequest(9999, "EchoService", "echo", payload);
    std::string frame = RpcCodec::encode(attack);

    std::cout << "构造恶意帧: " << frame.size() << " 字节，尝试发送...\n";

    // 3. 直接绕过框架，用裸 socket 发出去（这才是真正的黑客行为）
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9090);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect failed, 服务端没启动？");
        return 1;
    }
    write(fd, frame.data(), frame.size());
    std::cout << "已发送，等待服务端响应...\n";

    // 4. 读取服务端的响应
    char buf[4096];
    int n = read(fd, buf, sizeof(buf));
    if (n > 0) {
        RpcMessage resp;
        if (RpcCodec::decode(buf, n, resp)) {
            std::cout << "收到响应 type=" << (int)resp.type << "\n";
            
            // 模拟 RpcClient 内部的操作：剥壳！
            JsonSerializer serializer;
            auto parsed = serializer.parseResponse(resp.payload);
            
            if (parsed.ok) {
                std::cout << "✅ 成功结果: " << parsed.value.dump() << "\n";
            } else {
                std::cout << "❌ 提取出的干净错误码: " << parsed.error.code << "\n";
                std::cout << "❌ 提取出的干净错误信息: " << parsed.error.message << "\n";
            }
        }
    }
    close(fd);
    return 0;
}