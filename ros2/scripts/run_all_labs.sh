#!/bin/bash
# run_all_labs.sh — ROS2 实验一键启动菜单
set -e

# 两个工作区都相对本脚本定位 —— 这样整个仓库可以随便挪/换机器/换用户, 不用改脚本
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WS="$(cd "$SCRIPT_DIR/.." && pwd)"                 # ros2/     (本脚本所在工作区)
ZENOH_DIR="$(cd "$SCRIPT_DIR/../../zenoh" && pwd)" # zenoh/    (与 ros2/ 平级的独立工作区)
source /opt/ros/humble/setup.bash
source "$WS/install/setup.bash" 2>/dev/null || true

show_menu() {
    echo ""
    echo "╔══════════════════════════════════════════╗"
    echo "║  🤖 ROS2 实验 — 请选择                   ║"
    echo "╠══════════════════════════════════════════╣"
    echo "║  1) topic      基础Pub/Sub + 自定义消息  ║"
    echo "║  2) service    Service 请求/响应         ║"
    echo "║  3) action     Action 异步任务           ║"
    echo "║  4) qos        QoS 对比实验              ║"
    echo "║  5) lifecycle  Lifecycle 生命周期        ║"
    echo "║  6) compose    进程内 Composition        ║"
    echo "║  7) param      动态参数                  ║"
    echo "║  8) launch     Launch 文件编排           ║"
    echo "║  9) tf2        坐标变换 (TF2)            ║"
    echo "║ 10) rosbag2    录制/回放 (Rosbag2)       ║"
    echo "║ 11) loan       零拷贝 (Loan Message)     ║"
    echo "║ 12) multimachine 多机分布式通信          ║"
    echo "║ 13) dds_vendor DDS 供应商切换            ║"
    echo "║ 14) mqtt       MQTT 桥接                 ║"
    echo "║ 15) dds_bridge DDS↔ROS2 桥接             ║"
    echo "║ 16) build      编译全部包                ║"
    echo "║ 17) zenoh      Zenoh: DDS⇄Zenoh 切换/桥接║"
    echo "║  q) 退出                                ║"
    echo "╚══════════════════════════════════════════╝"
    echo ""
    read -r -p "输入 [1-17/q]: " c
    case "$c" in
        1) echo "--- 基础 Topic ---"; ros2 run topic_lab basic_pub & sleep 1; ros2 run topic_lab basic_sub; pkill basic_pub ;;
        2) echo "--- Service ---"; ros2 run service_lab battery_server & sleep 1; timeout 10 ros2 run service_lab battery_client; pkill battery_server ;;
        3) echo "--- Action ---"; ros2 run action_lab navigate_server & sleep 1; timeout 8 ros2 run action_lab navigate_client; pkill navigate_server ;;
        4) echo "--- QoS ---"; ros2 run qos_lab qos_subscriber --ros-args -p qos_mode:=reliable & sleep 1; timeout 8 ros2 run qos_lab qos_publisher --ros-args -p qos_mode:=reliable; pkill qos_subscriber ;;
        5) echo "--- Lifecycle ---"; ros2 run lifecycle_lab lifecycle_demo ;;
        6) echo "--- Composition ---"; timeout 8 ros2 run composition_lab composition_demo ;;
        7) echo "--- Parameter ---"; timeout 10 ros2 run param_lab param_demo ;;
        8) echo "--- Launch ---"; ros2 launch launch_lab demo_launch.py ;;
        9) echo "--- TF2 ---"; ros2 launch tf2_lab tf2_demo.launch.py ;;
        10) echo "--- Rosbag2 ---"; ros2 run rosbag2_lab data_generator & sleep 1; timeout 10 ros2 run rosbag2_lab bag_recorder; pkill data_generator; echo "录制完成, 回放..."; timeout 5 ros2 run rosbag2_lab bag_player ;;
        11) echo "--- Loan Message ---"; ros2 run loan_lab loan_subscriber & sleep 1; timeout 10 ros2 run loan_lab loan_publisher; pkill loan_subscriber ;;
        12) echo "--- Multi-Machine ---"; ros2 run multimachine_lab discovery_subscriber --ros-args -p domain_id:=0 & sleep 1; timeout 8 ros2 run multimachine_lab discovery_publisher --ros-args -p domain_id:=0; pkill discovery_subscriber ;;
        13) echo "--- DDS Vendor ---"; timeout 8 ros2 run dds_vendor_lab vendor_test_node --ros-args -p mode:=pub ;;
        14) echo "--- MQTT ---"; timeout 10 ros2 run mqtt_lab ros2_mqtt_bridge ;;
        15) echo "--- DDS Bridge ---"; ros2 run dds_bridge dds_ros2_bridge ;;
        16) echo "--- Build All ---"; cd "$WS"; colcon build --packages-select topic_lab service_lab action_lab qos_lab lifecycle_lab composition_lab param_lab launch_lab tf2_lab rosbag2_lab loan_lab multimachine_lab dds_vendor_lab mqtt_lab dds_bridge ;;
        17) echo "--- Zenoh (DDS ⇄ Zenoh 切换/桥接) ---"
            read -r -p "  子命令 [switch/bridge/crossnet/mixed/all, 默认 all]: " z
            bash "$ZENOH_DIR/scripts/run_demo.sh" "${z:-all}" ;;
        q) exit 0 ;;
    esac
}

while true; do show_menu; done
