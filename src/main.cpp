#include <EventLoop.h>
#include <iostream>

int main() {
    std::cout << "Starting My Light Framework..." << std::endl;
    
    // 创建 EventLoop 对象
    EventLoop loop;
    std::cout << "EventLoop created successfully!" << std::endl;
    
    // 暂时不进入事件循环，仅用于验证链接
    return 0;
}