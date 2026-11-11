cmake_minimum_required(VERSION 3.10)
project(MyLightFramework)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# muduo-core 路径
set(MUDUO_DIR "/home/lee/projects/muduo-core")
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${MUDUO_DIR}/include)
link_directories(${MUDUO_DIR}/lib)

# 可执行文件
add_executable(server src/main.cpp)
target_link_libraries(server muduo_core pthread)