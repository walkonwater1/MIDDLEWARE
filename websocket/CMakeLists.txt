cmake_minimum_required(VERSION 3.16)
project(websocket-signaling-demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# websocketpp — header-only 库, 无 pkg-config/CMake config
# ============================================================
find_file(WEBSOCKETPP_SERVER websocketpp/server.hpp)
if(NOT WEBSOCKETPP_SERVER)
    message(FATAL_ERROR
        "未找到 websocketpp/server.hpp!\n"
        "请安装: sudo apt-get install libwebsocketpp-dev")
endif()

# 依赖 Boost.Asio (system)
find_package(Boost REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)

# ============================================================
# 共享 include 路径
# ============================================================
set(COMMON_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR})

# ============================================================
# 服务端 — 信令服务器 + 数据处理引擎
# ============================================================
add_executable(ws-server
    server/demo_server.cpp
    server/ws_signaling_server.cpp
    server/data_processor.cpp
)
target_include_directories(ws-server PRIVATE ${COMMON_INCLUDE})
target_link_libraries(ws-server
    PRIVATE
    Boost::system
    Threads::Threads
)

# ============================================================
# 客户端 — 信令客户端 + 数据推流器 + 数据接收器
# ============================================================
add_executable(ws-client
    client/demo_client.cpp
    client/ws_signaling_client.cpp
    client/data_publisher.cpp
    client/data_receiver.cpp
)
target_include_directories(ws-client PRIVATE ${COMMON_INCLUDE})
target_link_libraries(ws-client
    PRIVATE
    Boost::system
    Threads::Threads
)
