cmake_minimum_required(VERSION 3.10)
project(MyLightFramework)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- muduo 库（vendored in third_party/）----
set(MUDUO_DIR "${PROJECT_SOURCE_DIR}/third_party/muduo")
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${MUDUO_DIR}/include)

set(NLOHMANN_DIR "${PROJECT_SOURCE_DIR}/third_party/nlohmann")
include_directories(${PROJECT_SOURCE_DIR}/third_party)
link_directories(${MUDUO_DIR}/lib)

# 框架库源文件（不含 main）
set(FRAMEWORK_SOURCES
    src/Logger.cc
    src/Config.cc
    src/RpcValue.cc
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
target_link_libraries(rpc_server muduo_net muduo_base pthread)

# ---- RPC 客户端 ----
add_executable(rpc_client
    src/client_main.cpp
    ${FRAMEWORK_SOURCES}
)
target_link_libraries(rpc_client muduo_net muduo_base pthread)

# ---- 协议 Round-Trip 单元测试 ----
add_executable(test_codec_roundtrip
    src/test/test_codec_roundtrip.cpp
    src/RpcMessage.cc
    src/RpcCodec.cc
    src/Serializer.cc
    src/RpcValue.cc
    src/Logger.cc
)
target_link_libraries(test_codec_roundtrip muduo_net muduo_base pthread)

# ---- 超时机制测试 ----
add_executable(test_timeout
    src/test/test_timeout.cpp
    ${FRAMEWORK_SOURCES}
)
target_link_libraries(test_timeout muduo_net muduo_base pthread)

# ---- DoS 抗攻击测试 ----
add_executable(test_server_dos
    src/test/test_server_dos.cpp
    src/RpcMessage.cc
    src/RpcCodec.cc
    src/Serializer.cc
    src/RpcValue.cc
    src/Logger.cc
)
target_link_libraries(test_server_dos muduo_net muduo_base pthread)

# ---- 连接池性能基准测试 ----
add_executable(benchmark_connection_pool
    src/test/benchmark_connection_pool.cpp
    ${FRAMEWORK_SOURCES}
)
target_link_libraries(benchmark_connection_pool muduo_net muduo_base pthread)

# ---- 一体化 Demo（原有）----
add_executable(server
    src/main.cpp
    ${FRAMEWORK_SOURCES}
)
target_link_libraries(server muduo_net muduo_base pthread)