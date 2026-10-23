cmake_minimum_required(VERSION 3.16)
project(zenoh_native_lab CXX)

# ============================================================
#  原生 Zenoh 示例 —— 与 ROS2 完全无关
#
#  zenoh-cpp (zenoh.hxx) 是 header-only 的, 真正链接的是 zenoh-c:
#  ROS2 Humble 的 rmw_zenoh_cpp 已经把 libzenohc.so 装到了机器上,
#  所以这里**不需要另外安装 Zenoh**, 直接复用那一份。
#
#  默认路径 = zenoh_cpp_vendor 的安装前缀。换机器 / 自己编了 zenoh-c
#  的话, 用 -DZENOH_PREFIX=/your/prefix 覆盖。
# ============================================================
set(ZENOH_PREFIX "/opt/ros/humble/opt/zenoh_cpp_vendor" CACHE PATH
    "zenoh-c installation prefix (contains include/ and lib/cmake/zenohc)")

list(APPEND CMAKE_PREFIX_PATH "${ZENOH_PREFIX}")
find_package(zenohc REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(zenoh_native_common INTERFACE)
target_include_directories(zenoh_native_common INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# zenoh-cpp 要求显式声明后端 (zenoh-c 还是 zenoh-pico)
target_compile_definitions(zenoh_native_common INTERFACE ZENOHCXX_ZENOHC)
target_link_libraries(zenoh_native_common INTERFACE zenohc::lib)

foreach(demo z_pub z_sub z_queryable z_get z_sysmon)
  add_executable(${demo} src/${demo}.cpp)
  target_link_libraries(${demo} PRIVATE zenoh_native_common)
endforeach()

install(TARGETS z_pub z_sub z_queryable z_get z_sysmon DESTINATION lib/${PROJECT_NAME})
