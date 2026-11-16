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

# ---- 一体化 Demo（原有）----
add_executable(server
    src/main.cpp
    ${FRAMEWORK_SOURCES}
)
target_link_libraries(server muduo_net muduo_base pthread)