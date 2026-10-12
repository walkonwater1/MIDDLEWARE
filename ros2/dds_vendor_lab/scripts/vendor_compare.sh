#!/bin/bash
# vendor_compare.sh — FastDDS vs CycloneDDS 自动对比脚本
set -e

echo "╔══════════════════════════════════════════╗"
echo "║  DDS 供应商对比: FastDDS vs CycloneDDS  ║"
echo "╚══════════════════════════════════════════╝"
echo ""

# 检查 CycloneDDS 是否已安装
if ! dpkg -l | grep -q "ros-humble-rmw-cyclonedds-cpp"; then
    echo "⚠️  rmw_cyclonedds_cpp 未安装"
    echo "   安装: sudo apt install ros-humble-rmw-cyclonedds-cpp"
    echo ""
fi

source /opt/ros/humble/setup.bash
# 相对本脚本定位 ros2/ 工作区: scripts/ → dds_vendor_lab/ → ros2/
WS="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$WS/install/setup.bash" 2>/dev/null || true

# --------------------------------------------------
# 列出可用的 RMW 实现
# --------------------------------------------------
echo "━━━ 可用的 RMW 实现 ━━━"
echo "  当前 RMW: ${RMW_IMPLEMENTATION:-默认 (FastDDS)}"
echo ""

# 检查已安装的 rmw 包
echo "  已安装的 RMW 包:"
dpkg -l 2>/dev/null | grep "ros-humble-rmw-" | awk '{print "    ✅ " $2}' || echo "    (无)"
echo ""

# --------------------------------------------------
# FastDDS 测试 (默认)
# --------------------------------------------------
echo "━━━ 测试 1: FastDDS (默认) ━━━"
echo "  启动 vendor_test_node (pub) 5 秒..."

export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
timeout 5 ros2 run dds_vendor_lab vendor_test_node --ros-args -p mode:=pub &
PID=$!
sleep 5
wait $PID 2>/dev/null || true
echo "  完成"
echo ""

# --------------------------------------------------
# CycloneDDS 测试 (如果已安装)
# --------------------------------------------------
echo "━━━ 测试 2: CycloneDDS ━━━"
if dpkg -l | grep -q "ros-humble-rmw-cyclonedds-cpp"; then
    echo "  启动 vendor_test_node (pub) 5 秒..."

    export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
    timeout 5 ros2 run dds_vendor_lab vendor_test_node --ros-args -p mode:=pub &
    PID=$!
    sleep 5
    wait $PID 2>/dev/null || true
    echo "  完成"
else
    echo "  ⏭️  跳过 (rmw_cyclonedds_cpp 未安装)"
fi
echo ""

# --------------------------------------------------
# 诊断工具
# --------------------------------------------------
echo "━━━ ROS2 诊断 ━━━"
echo "  ros2 doctor 报告:"
ros2 doctor 2>/dev/null || echo "  (ros2 doctor 不可用)"
echo ""
echo "  ros2 wtf 详细诊断:"
ros2 wtf 2>/dev/null || echo "  (ros2 wtf 不可用)"
echo ""

echo "━━━ 对比总结 ━━━"
echo "  维度         FastDDS         CycloneDDS"
echo "  ─────────    ───────────     ───────────"
echo "  ROS2 默认    ✅ Humble 默认  需手动安装"
echo "  支持协作     ✅               ✅"
echo "  发现速度     较慢 (~30s)     较快 (<5s)"
echo "  内存占用     ~50MB           ~30MB"
echo "  零拷贝       LoanableSeq     shared memory loan"
echo "  适用场景     通用 ROS2 开发  资源受限嵌入式"
echo ""
echo "  切换方式:"
echo "    export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp"
echo "    ros2 run <pkg> <exe>"
echo ""
echo "  诊断命令:"
echo "    ros2 doctor      # 环境诊断"
echo "    ros2 wtf         # 详细诊断"
echo "    ros2 topic list  # 验证发现"
echo ""
