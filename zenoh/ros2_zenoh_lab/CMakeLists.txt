cmake_minimum_required(VERSION 3.16)
project(ros2_zenoh_lab)

# ============================================================
#  依赖 — 只依赖 ROS2 自带件, 刻意不链接任何 DDS/Zenoh 库
#  (换传输 = 换 RMW_IMPLEMENTATION 环境变量, 代码零变更)
# ============================================================
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(rmw_implementation REQUIRED)

# ============================================================
#  rmw_probe_node — RMW 无关的传输自省节点
# ============================================================
add_executable(rmw_probe_node src/rmw_probe_node.cpp)
ament_target_dependencies(rmw_probe_node rclcpp std_msgs rmw_implementation)

# ============================================================
#  安装
# ============================================================
install(TARGETS rmw_probe_node DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
