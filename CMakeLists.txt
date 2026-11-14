cmake_minimum_required(VERSION 3.10)
project(MyLightFramework)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# muduo-core 路径
set(MUDUO_DIR "/home/lee/projects/muduo-core")
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${MUDUO_DIR}/include)
link_directories(${MUDUO_DIR}/lib)

# 框架库源文件（不含 main）
set(FRAMEWORK_SOURCES
    src/Logger.cc
    src/Config.cc
    src/RpcMessage.cc
    src/Serializer.cc
    src/RpcCodec.cc
    src/TcpServerAdapter.cc
    src/RpcServer.cc
    src/RpcClient.cc
    src/Application.cc
)

# ---- RPC 服务端 ----
add_executable(rpc_server
    src/server_main.cpp
    ${FRAMEWORK_SOURCES}
)
target_link_libraries(rpc_server muduo_core pthread)

# ---- RPC 客户端 ----
add_executable(rpc_client
    src/client_main.cpp
    ${FRAMEWORK_SOURCES}
)
target_link_libraries(rpc_client muduo_core pthread)

# ---- 一体化 Demo（原有）----
add_executable(server
    src/main.cpp
    ${FRAMEWORK_SOURCES}
)
target_link_libraries(server muduo_core pthread)