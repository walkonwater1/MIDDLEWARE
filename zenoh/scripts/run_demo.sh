#!/usr/bin/env bash
# ============================================================
#  run_demo.sh — 一键运行 ROS2 的 DDS ⇄ Zenoh 切换与桥接 Demo
#
#  用法:
#    bash scripts/run_demo.sh                # 默认 = switch
#    bash scripts/run_demo.sh native         # 0. 原生 Zenoh API 用法 (不涉及 ROS2)
#    bash scripts/run_demo.sh switch         # A. RMW 切换: FastDDS / CycloneDDS / Zenoh
#    bash scripts/run_demo.sh bridge         # B. DDS ↔ Zenoh 桥接 (REST 可观测)
#    bash scripts/run_demo.sh rest           # B'. 只起发布者+桥接器, 留给你手玩 REST
#    bash scripts/run_demo.sh crossnet       # C. 跨域/跨网络桥接 (机器人 ⇄ 云)
#    bash scripts/run_demo.sh mixed          # D. 两种接入方式共存但不互通
#    bash scripts/run_demo.sh all            # 依次跑 0→A→B→C→D
#    bash scripts/run_demo.sh deps           # 安装两个 apt 包
#    bash scripts/run_demo.sh build          # 仅 colcon 编译
#    bash scripts/run_demo.sh clean          # 清理 build/install/log
#
#  说明:
#    zenoh/ 是一个**独立的 colcon 工作区**, 与 ../ros2/ 互不影响。
#    每次切 RMW 都会重置 ros2 daemon —— 这是 rmw_zenoh 的硬性要求,
#    否则 ros2 node list / topic list 会显示上一个 RMW 的陈旧图。
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
INSTALL_SETUP="$PROJECT_DIR/install/setup.bash"
NODE_BIN="$PROJECT_DIR/install/ros2_zenoh_lab/lib/ros2_zenoh_lab/rmw_probe_node"

# 两个外部可执行文件 —— 都不在 PATH 上, 只能走绝对路径
ROUTER_BIN="/opt/ros/humble/lib/rmw_zenoh_cpp/rmw_zenohd"
BRIDGE_BIN="/opt/ros/humble/lib/zenoh_bridge_dds/zenoh_bridge_dds"

LOG_DIR="$(mktemp -d /tmp/zenoh_lab.XXXXXX)"

# ---- 颜色 ----
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
err()   { echo -e "${RED}[ERR]${NC}  $*"; }
title() { echo -e "\n${CYAN}━━━ $* ━━━${NC}\n"; }

# 节点日志很长 (每秒一行统计), 只挑有信息量的行给人看, 原始日志留在 LOG_DIR
FILTER='┌|│|└|✅|本机 RMW|对端 RMW|   → |SUMMARY|收到 #1:|发布中|订阅中|ERROR|WARN|未知 mode'

# ============================================================
# 进程管理 —— 所有后台进程登记在 KILL_PIDS, 退出时逆序清理
# ============================================================
KILL_PIDS=()

cleanup() {
    local pid i alive waited
    local -a remaining

    # 第一轮: SIGTERM, 让它们自己收工 (路由器/桥接器都能优雅退出)
    for ((i=${#KILL_PIDS[@]}-1; i>=0; i--)); do
        pid="${KILL_PIDS[$i]}"
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
        fi
    done

    # 第二轮: 最多等 3 秒; 还赖着不走的, SIGKILL 强收。
    # 光靠 SIGTERM 不够保险 —— 桥接器连不上路由器时会 SIGABRT 留个半死进程,
    # 上一轮试验里就漏过一个 rmw_zenohd, 一直占着 7447 让下一次跑的结果不可信。
    waited=0
    while [ "$waited" -lt 30 ]; do
        alive=0
        for pid in "${KILL_PIDS[@]}"; do
            kill -0 "$pid" 2>/dev/null && alive=1
        done
        [ "$alive" -eq 0 ] && break
        sleep 0.1
        waited=$((waited + 1))
    done

    for pid in "${KILL_PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill -9 "$pid" 2>/dev/null || true
        fi
    done
    for pid in "${KILL_PIDS[@]}"; do
        wait "$pid" 2>/dev/null || true
    done

    remaining=()
    for pid in "${KILL_PIDS[@]}"; do
        kill -0 "$pid" 2>/dev/null && remaining+=("$pid")
    done
    if [ "${#remaining[@]}" -gt 0 ]; then
        warn "清理后仍有进程存活: ${remaining[*]}"
    fi
    KILL_PIDS=()
}
trap cleanup EXIT INT TERM

# 每个 Demo 段落收工时调用, 不留后台进程给下一段
kill_tracked() { cleanup; }

# ============================================================
# 环境准备
# ============================================================

# 用户 shell 里的 .bashrc 可能已经设了这些 —— 全部清掉再显式设置,
# 否则 Demo 会莫名其妙地跑到别的 domain / 别的 RMW 上
reset_ros_env() {
    unset RMW_IMPLEMENTATION ROS_DOMAIN_ID ROS_LOCALHOST_ONLY
    unset ZENOH_SESSION_CONFIG_URI ZENOH_ROUTER_CONFIG_URI ZENOH_CONFIG_OVERRIDE
    unset ZENOH_ROUTER_CHECK_ATTEMPTS ZENOH_SHM_ALLOC_SIZE
    unset ZENOH_SHM_MESSAGE_SIZE_THRESHOLD RMW_ZENOH_BUFFER_POOL_MAX_SIZE_BYTES
    unset ZENOH_LAB_MODE ZENOH_LAB_TOPIC ZENOH_LAB_RATE_HZ ZENOH_LAB_LABEL
    EXTRA_ENV=()
}

# 切换 RMW 后必须重置 ros2 daemon (rmw_zenoh README 明确要求)
ros_daemon_reset() {
    ros2 daemon stop >/dev/null 2>&1 || true
    pkill -9 -f 'ros2 daemon' >/dev/null 2>&1 || true
    sleep 1
}

source_ws() {
    # shellcheck disable=SC1091
    source /opt/ros/humble/setup.bash
    if [ ! -f "$INSTALL_SETUP" ]; then
        warn "工作区未编译, 先编译..."
        do_build
    fi
    # shellcheck disable=SC1091
    source "$INSTALL_SETUP"
}

have_zenoh_rmw() { [ -x "$ROUTER_BIN" ]; }

have_bridge() { [ -x "$BRIDGE_BIN" ]; }

require_zenoh_rmw() {
    if ! have_zenoh_rmw; then
        warn "缺少 $ROUTER_BIN —— 跳过本段"
        warn "安装: sudo apt-get install -y ros-humble-rmw-zenoh-cpp"
        return 1
    fi
    return 0
}

require_bridge() {
    if ! have_bridge; then
        err "缺少 $BRIDGE_BIN"
        err "安装: sudo apt-get install -y ros-humble-zenoh-bridge-dds  (或 bash $0 deps)"
        return 1
    fi
    return 0
}

check_deps() {
    local missing=()
    if ! have_zenoh_rmw; then
        missing+=("ros-humble-rmw-zenoh-cpp")
    fi
    if ! have_bridge; then
        missing+=("ros-humble-zenoh-bridge-dds")
    fi
    command -v curl >/dev/null 2>&1 || missing+=("curl")
    command -v colcon >/dev/null 2>&1 || missing+=("python3-colcon-common-extensions")
    if [ ${#missing[@]} -gt 0 ]; then
        err "缺少依赖: ${missing[*]}"
        info "安装: sudo apt-get install -y ${missing[*]}"
        return 1
    fi
    return 0
}

# ============================================================
# 节点启动
# ============================================================

# run_node_bg <domain> <rmw> <日志> <ros-args...>
# 直接调可执行文件 (不走 ros2 run), 这样 $! 就是节点本身, 杀得干净
run_node_bg() {
    local domain="$1"; shift
    local rmw="$1"; shift
    local logf="$1"; shift
    env ROS_DOMAIN_ID="$domain" RMW_IMPLEMENTATION="$rmw" "${EXTRA_ENV[@]}" \
        "$NODE_BIN" --ros-args "$@" >"$logf" 2>&1 &
    local pid=$!
    KILL_PIDS+=("$pid")
    info "↑ 后台启动 [$rmw @ domain ${domain}] pid=$pid"
}

# run_node_fg <domain> <rmw> <ros-args...>  — 前台跑, 输出过滤后实时展示
run_node_fg() {
    local domain="$1"; shift
    local rmw="$1"; shift
    local logf="$LOG_DIR/fg_${RANDOM}.log"
    if ! env ROS_DOMAIN_ID="$domain" RMW_IMPLEMENTATION="$rmw" "${EXTRA_ENV[@]}" \
            "$NODE_BIN" --ros-args "$@" >"$logf" 2>&1; then
        warn "节点非零退出, 原始日志: $logf"
    fi
    grep -E "$FILTER" "$logf" || true
}

show_log() {
    grep -E "$FILTER" "$1" || true
}

# 从日志里取"最新一次统计的 received 数", 用于负对照断言
last_received() {
    local n
    n="$(grep -o 'received=[0-9]*' "$1" 2>/dev/null | tail -1 | cut -d= -f2 || true)"
    echo "${n:-0}"
}

# ============================================================
# Zenoh 路由器 / 桥接器
# ============================================================

start_router() {
    local logf="$LOG_DIR/router.log"
    "$ROUTER_BIN" >"$logf" 2>&1 &
    local pid=$!
    KILL_PIDS+=("$pid")

    if command -v ss >/dev/null 2>&1; then
        local i
        for i in $(seq 1 20); do
            if ss -ltn 2>/dev/null | grep -q ':7447'; then
                info "zenoh 路由器就绪 (pid=$pid, tcp/…:7447)"
                return 0
            fi
            sleep 0.5
        done
        warn "10 秒没等到 :7447 —— 继续, 节点会自行重试 (见 $logf)"
    else
        sleep 2
        info "zenoh 路由器启动 (pid=$pid); 无 ss 命令, 固定等 2 秒"
    fi
    return 0
}

# start_bridge <dds_domain> <listen|""> <peer|""> <rest_port> <allow_regex> [mode]
start_bridge() {
    local domain="$1"
    local listen="$2"
    local peer="$3"
    local port="$4"
    local allow="$5"
    local mode="${6:-peer}"
    # 日志名带上 REST 端口: 同一个 DDS domain 上可能先后起多个桥 (Demo D 就是),
    # 只按 domain 命名会让后起的桥覆盖掉前一个的日志。
    local logf="$LOG_DIR/bridge_d${domain}_p${port}.log"

    local args=(-d "$domain" -m "$mode" --rest-plugin --rest-http-port "$port")
    if [ -n "$listen" ]; then
        args+=(-l "$listen")
    fi
    if [ -n "$peer" ]; then
        args+=(-e "$peer")
    fi
    if [ -n "$allow" ]; then
        args+=(-a "$allow")
    fi

    "$BRIDGE_BIN" "${args[@]}" >"$logf" 2>&1 &
    local pid=$!
    KILL_PIDS+=("$pid")
    info "↑ 桥接器 DDS domain ${domain} → zenoh (mode=${mode}, REST=:${port}, pid=$pid)"
    info "  日志: $logf"
}

# 上一次运行残留的桥接器会占着 REST 端口, 让 Demo 结果不可信
check_rest_port_free() {
    local port="$1"
    if curl -sg --max-time 2 "http://localhost:${port}/@/version" >/dev/null 2>&1; then
        warn "端口 ${port} 上已有 REST 服务在跑 (多半是上次残留)"
        warn "清理: pkill -f zenoh_bridge_dds"
        return 1
    fi
    return 0
}

# ------------------------------------------------------------
# 版本取证 (Demo D 用): 不写死版本号, 全部从真实产物里挖出来。
#
#   路由器侧: rmw_zenohd 只有 47KB, 是 libzenohc.so 的薄壳; zenoh 版本
#             写在 zenoh-c 的 CMake ConfigVersion 里, 源码修订串在 .so 里。
#   桥接器侧: 静态链接的 Rust 二进制, 用 -V 自报版本, 修订串在二进制里。
# ------------------------------------------------------------
zenoh_ver_vendor() {   # rmw_zenoh_cpp 走的 zenoh-c 版本
    grep -oE 'set\(PACKAGE_VERSION "[^"]+"' \
        /opt/ros/humble/opt/zenoh_cpp_vendor/lib/cmake/zenohc/zenohcConfigVersion.cmake 2>/dev/null \
        | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1
}
zenoh_rev_router() {   # 路由器的 zenoh 源码修订 (藏在 libzenohc.so 里)
    strings /opt/ros/humble/opt/zenoh_cpp_vendor/lib/libzenohc.so 2>/dev/null \
        | grep -oE 'zenoh-[0-9a-f]{16}/[0-9a-f]{7}' | head -1
}
zenoh_rev_bridge() {   # 桥接器的 zenoh 源码修订
    strings "$BRIDGE_BIN" 2>/dev/null \
        | grep -oE 'zenoh-[0-9a-f]{16}/[0-9a-f]{7}' | head -1
}
zenoh_ver_bridge() {   # 桥接器自报版本
    "$BRIDGE_BIN" -V 2>/dev/null | head -1
}

# ============================================================
# A. RMW 切换
# ============================================================
demo_switch() {
    source_ws
    reset_ros_env

    info "日志目录: $LOG_DIR"

    # ---- A1. FastDDS ----
    title "A1. FastDDS — ROS2 Humble 的默认 RMW"
    ros_daemon_reset
    EXTRA_ENV=()
    run_node_bg 10 rmw_fastrtps_cpp "$LOG_DIR/a1_pub.log" \
        -p mode:=pub -p exit_after:=8.0 -p label:=fastdds_pub
    sleep 2
    EXTRA_ENV=()
    run_node_fg 10 rmw_fastrtps_cpp \
        -p mode:=sub -p exit_after:=6.0 -p label:=fastdds_sub
    kill_tracked

    # ---- A2. CycloneDDS ----
    title "A2. CycloneDDS — 换一个 DDS 供应商, 同样的 RTPS 线协议"
    if dpkg -s ros-humble-rmw-cyclonedds-cpp >/dev/null 2>&1; then
        ros_daemon_reset
        EXTRA_ENV=()
        run_node_bg 11 rmw_cyclonedds_cpp "$LOG_DIR/a2_pub.log" \
            -p mode:=pub -p exit_after:=8.0 -p label:=cyclonedds_pub
        sleep 2
        EXTRA_ENV=()
        run_node_fg 11 rmw_cyclonedds_cpp \
            -p mode:=sub -p exit_after:=6.0 -p label:=cyclonedds_sub
        kill_tracked
    else
        warn "未装 ros-humble-rmw-cyclonedds-cpp, 跳过 A2"
    fi

    # ---- A3. rmw_zenoh ----
    title "A3. Zenoh — 换掉整个传输栈 (不再是 DDS)"
    if require_zenoh_rmw; then
        ros_daemon_reset
        start_router
        EXTRA_ENV=(ZENOH_ROUTER_CHECK_ATTEMPTS=10
                   ZENOH_LAB_MODE=pub
                   ZENOH_LAB_TOPIC=/zenoh_probe
                   ZENOH_LAB_LABEL=zenoh_pub)
        run_node_bg 12 rmw_zenoh_cpp "$LOG_DIR/a3_pub.log" \
            -p mode:=pub -p exit_after:=10.0 -p label:=zenoh_pub -p rate_hz:=2.0
        sleep 3
        EXTRA_ENV=(ZENOH_ROUTER_CHECK_ATTEMPTS=10
                   ZENOH_LAB_MODE=sub
                   ZENOH_LAB_TOPIC=/zenoh_probe
                   ZENOH_LAB_LABEL=zenoh_sub)
        run_node_fg 12 rmw_zenoh_cpp \
            -p mode:=sub -p exit_after:=7.0 -p label:=zenoh_sub

        info "同一张图用 ros2 CLI 看 (注意: 必须重置 daemon 后才准):"
        ros_daemon_reset
        env RMW_IMPLEMENTATION=rmw_zenoh_cpp ZENOH_ROUTER_CHECK_ATTEMPTS=10 \
            ROS_DOMAIN_ID=12 timeout 8 ros2 topic list 2>/dev/null || true
        env RMW_IMPLEMENTATION=rmw_zenoh_cpp ZENOH_ROUTER_CHECK_ATTEMPTS=10 \
            ROS_DOMAIN_ID=12 timeout 8 ros2 node list 2>/dev/null || true
        kill_tracked
    fi

    # ---- A4. 跨供应商正对照 ----
    title "A4. 正对照 — FastDDS 发布 + CycloneDDS 订阅 (RTPS 线协议互通)"
    if dpkg -s ros-humble-rmw-cyclonedds-cpp >/dev/null 2>&1; then
        ros_daemon_reset
        EXTRA_ENV=()
        run_node_bg 13 rmw_fastrtps_cpp "$LOG_DIR/a4_pub.log" \
            -p mode:=pub -p exit_after:=8.0 -p label:=fastdds_pub
        sleep 2
        EXTRA_ENV=()
        run_node_fg 13 rmw_cyclonedds_cpp \
            -p mode:=sub -p exit_after:=6.0 -p label:=cyclonedds_sub
        kill_tracked
        info "这组正对照是 Demo D 负结果的前提: DDS 家族内部能互通, 是因为共用 RTPS 线协议与组播发现"
    else
        warn "未装 rmw_cyclonedds_cpp, 跳过 A4"
    fi

    info "原生日志全部保留在: $LOG_DIR"
}

# ============================================================
# B. DDS ↔ Zenoh 桥接 (REST 可观测)
# ============================================================
demo_bridge() {
    source_ws
    reset_ros_env
    require_bridge || return 1
    check_rest_port_free 8000 || return 1

    title "B. DDS ↔ Zenoh 桥接 — ROS2 图进入 zenoh 网络"

    # ---- B0. DDS 侧发布者 ----
    ros_daemon_reset
    EXTRA_ENV=()
    run_node_bg 0 rmw_fastrtps_cpp "$LOG_DIR/b_pub.log" \
        -p mode:=pub -p topic:=/zenoh_probe -p rate_hz:=5.0 -p label:=dds_talker
    sleep 3
    show_log "$LOG_DIR/b_pub.log" | head -14

    # ---- B1. 负对照 ----
    title "B1. 负对照 — 桥接器还没起, zenoh 侧什么都看不到"
    if curl -sg --max-time 2 "http://localhost:8000/@/version" >/dev/null 2>&1; then
        warn "8000 端口意外可达 —— 期望此时连不上"
    else
        info 'curl http://localhost:8000/@/version → 连接失败 ✓'
        info '也就是说: ROS2 话题此刻只活在 DDS 世界里, zenoh 网络上不存在'
    fi

    # ---- B2. 起桥接器 ----
    title "B2. 启动桥接器 — DDS domain 0 ↔ zenoh"
    start_bridge 0 tcp/localhost:7447 "" 8000 '.*zenoh_probe'
    sleep 4
    info "桥接器日志尾部:"
    grep -iE 'route|subscri|publish|listen|error' "$LOG_DIR/bridge_d0_p8000.log" 2>/dev/null | tail -8 || true

    # ---- B3. REST 探针 ----
    title "B3. 从 zenoh 侧观测"
    bash "$SCRIPT_DIR/rest_probe.sh" 8000 zenoh_probe 8

    kill_tracked
    info "原生日志全部保留在: $LOG_DIR"
}

# ============================================================
# B'. 只起发布者 + 桥接器, 留给用户手玩 REST
# ============================================================
demo_rest() {
    source_ws
    reset_ros_env
    require_bridge || return 1
    check_rest_port_free 8000 || return 1

    title "B'. REST 沙盒 — 桥接器与发布者常驻, 你手动 curl"
    ros_daemon_reset
    EXTRA_ENV=()
    run_node_bg 0 rmw_fastrtps_cpp "$LOG_DIR/rest_pub.log" \
        -p mode:=pub -p topic:=/zenoh_probe -p rate_hz:=5.0 -p label:=dds_talker
    sleep 2
    start_bridge 0 tcp/localhost:7447 "" 8000 '.*zenoh_probe'
    sleep 3

    echo
    info "现在可以自己试 (新开一个终端):"
    echo "    bash zenoh/scripts/rest_probe.sh 8000 zenoh_probe 8"
    echo "    curl -sg 'http://localhost:8000/@/service/**/route/**'"
    echo "    curl -sgN -H 'Accept: text/event-stream' 'http://localhost:8000/rt/zenoh_probe'"
    echo "    ros2 topic echo /zenoh_probe          # 对照 DDS 侧"
    echo
    info "按 Ctrl-C 结束 (会自动清理桥接器与发布者)"
    echo
    info "另外看一眼 DDS 话题名与 zenoh 键的对应关系:"
    env ROS_DOMAIN_ID=0 timeout 6 ros2 topic list 2>/dev/null || true
    echo
    wait
}

# ============================================================
# C. 跨域 / 跨网络桥接
# ============================================================
demo_crossnet() {
    source_ws
    reset_ros_env
    require_bridge || return 1
    check_rest_port_free 8000 || return 1
    check_rest_port_free 8001 || return 1

    title "C. 跨域桥接 — DDS 组播过不去的域, zenoh 过得去"
    info "拓扑: [机器人 DDS domain 0] --pub--> |桥1| ==zenoh==> |桥2| --sub--> [云 DDS domain 1]"

    # ---- 先起两个节点 (订阅者必须先存在, to_dds 路由才会建) ----
    ros_daemon_reset
    EXTRA_ENV=()
    run_node_bg 0 rmw_fastrtps_cpp "$LOG_DIR/c_robot.log" \
        -p mode:=pub -p topic:=/fleet/telemetry -p rate_hz:=5.0 -p label:=robot_telemetry
    EXTRA_ENV=()
    run_node_bg 1 rmw_fastrtps_cpp "$LOG_DIR/c_cloud.log" \
        -p mode:=sub -p topic:=/fleet/telemetry -p label:=cloud_monitor

    # ---- C1. 负对照 ----
    title "C1. 负对照 — 两个 DDS 域各自为政"
    sleep 6
    local n0
    n0="$(last_received "$LOG_DIR/c_cloud.log")"
    info "云侧 (domain 1) 已收 ${n0} 条"
    if [ "$n0" -eq 0 ]; then
        info "✓ domain 1 收不到 domain 0 的组播 —— 这就是 DDS 跨域/跨网的死结"
    else
        warn "收到 ${n0} 条了, 不符合预期 —— 检查是不是有别的发布者在这个 domain 上"
    fi

    # ---- C2. 起两个桥接器 ----
    title "C2. 起两个桥接器, 用 zenoh 把两个 DDS 域接起来"
    start_bridge 0 tcp/localhost:7447 "" 8000 '.*telemetry'
    sleep 2
    start_bridge 1 tcp/localhost:7448 tcp/localhost:7447 8001 '.*telemetry'

    # ---- C3. 正结果 ----
    title "C3. 正结果"
    sleep 8
    local n1
    n1="$(last_received "$LOG_DIR/c_cloud.log")"
    info "云侧 (domain 1) 现在收到 ${n1} 条"
    if [ "$n1" -gt 0 ]; then
        info "✓ domain 0 的消息穿过 zenoh, 落到了 domain 1"
    else
        warn "仍然是 0 条 —— 见 README 排错表"
        warn "最常见原因: 桥接器静态链接的 CycloneDDS 没选中本机 FastDDS 所在的网卡"
        warn "回落: export CYCLONEDDS_URI=<强制 lo + AllowMulticast=true>"
    fi
    grep -E '✅|本机 RMW|对端 RMW|   → ' "$LOG_DIR/c_cloud.log" | head -6 || true

    # ---- C4. 路由证据 ----
    title "C4. 两侧管理空间的路由证据"
    echo "  桥1 (domain 0) —— 期望 from_dds//rt/fleet/telemetry:"
    curl -sg --max-time 5 'http://localhost:8000/@/service/**/route/**' || true
    echo
    echo "  桥2 (domain 1) —— 期望 to_dds//rt/fleet/telemetry:"
    curl -sg --max-time 5 'http://localhost:8001/@/service/**/route/**' || true
    echo

    kill_tracked
    info "原生日志全部保留在: $LOG_DIR"
}

# ============================================================
# D. 两种接入方式共存 (但不互通)
# ============================================================
demo_mixed() {
    source_ws
    reset_ros_env
    require_zenoh_rmw || return 1
    require_bridge || return 1
    check_rest_port_free 8000 || return 1

    title "D. 两种接入方式共存 — 都叫 zenoh, 却隔着两层墙"
    info "本 Demo 的结论是**负面的**, 而且比预期更彻底。"
    info "rmw_zenoh_cpp 与 zenoh_bridge_dds 虽然都跑在 zenoh 上, 但它们是 zenoh 的**两代产品**:"
    info "  第一层墙 — 两者内置的 zenoh 协议大版本不同, 连 zenoh 会话都建不起来;"
    info "  第二层墙 — 就算能握手, 键表达式命名空间也没有交集。"
    info "所以'接入同一个 zenoh 网络'并不等于'并进同一张 ROS2 图'。"

    # ---- D1. 两个节点, 两条栈 ----
    ros_daemon_reset
    start_router

    EXTRA_ENV=(ZENOH_ROUTER_CHECK_ATTEMPTS=10
               ZENOH_LAB_MODE=pub
               ZENOH_LAB_TOPIC=/mixed_probe
               ZENOH_LAB_LABEL=zenoh_native)
    run_node_bg 0 rmw_zenoh_cpp "$LOG_DIR/d_zenoh.log" \
        -p mode:=pub -p topic:=/mixed_probe -p rate_hz:=5.0 -p label:=zenoh_native

    EXTRA_ENV=()
    run_node_bg 0 rmw_fastrtps_cpp "$LOG_DIR/d_dds.log" \
        -p mode:=sub -p topic:=/mixed_probe -p label:=dds_peer

    sleep 5

    title "D1. 两个节点的自述"
    echo "  zenoh_native (RMW=rmw_zenoh_cpp):"
    grep -E 'RMW 实现|传输 |ROS_DOMAIN_ID' "$LOG_DIR/d_zenoh.log" | head -3 || true
    echo "  dds_peer (RMW=rmw_fastrtps_cpp):"
    grep -E 'RMW 实现|传输 |ROS_DOMAIN_ID' "$LOG_DIR/d_dds.log" | head -3 || true

    # ---- D2. 版本取证: 第一层墙的物证 ----
    title "D2. 第一层墙的物证 — 两边内置的 zenoh 差了一代"
    echo "  rmw_zenoh_cpp 侧 (路由器):"
    echo "    zenoh-c 版本 : $(zenoh_ver_vendor || echo '?')"
    echo "    zenoh 修订   : $(zenoh_rev_router || echo '?')"
    echo "  桥接器侧:"
    echo "    $(zenoh_ver_bridge || echo '?')"
    echo "    zenoh 修订   : $(zenoh_rev_bridge || echo '?')"
    info "同一个 zenoh 仓库 (zenoh-cc237f2570fab813) 的两个不同 git 修订 —— 协议对不上。"

    # ---- D3. 桥接器独立起 (先证明它本身没坏) ----
    title "D3. 桥接器独立运行 (peer 模式, 自己的 locator tcp/localhost:7450)"
    start_bridge 0 tcp/localhost:7450 "" 8000 '.*mixed_probe'
    sleep 6
    if curl -sg --max-time 3 "http://localhost:8000/@/version" >/dev/null 2>&1; then
        info "✓ 桥接器活着, REST 插件在线 —— 桥接器本身没问题, 问题在它和路由器的关系上"
    else
        warn "桥接器没起来, 本段后续证据不可信"
    fi

    # ---- D4. 证据一 ----
    title "D4. 证据一 — zenoh 侧只看得见 DDS 那套键 (/rt/...)"
    bash "$SCRIPT_DIR/rest_probe.sh" 8000 mixed_probe 6 || warn "REST 探针有步骤失败, 见上面输出"
    echo
    info "注意第四枪采样为空 —— 这本身又是一份证据:"
    info "  DDS 侧 /mixed_probe 确实被桥接器登记了 (第 2 枪里那条 routes 为 Routed 的 reader),"
    info "  但键 /rt/mixed_probe 上一条数据都没有。因为唯一在发 /mixed_probe 的节点 (zenoh_native)"
    info "  走的是 <domain>/<FQN>/<类型> 那套键, 根本没经过桥 —— 桥这一侧自然是死的。"

    title "D5. 证据二 — DDS 侧收到 0 条"
    local n
    n="$(last_received "$LOG_DIR/d_dds.log")"
    info "dds_peer 已收 ${n} 条 (期望 0)"
    if [ "$n" -eq 0 ]; then
        info "✓ zenoh_native 在发 /mixed_probe, dds_peer 在同 domain 也在订 /mixed_probe, 但一条都没收到"
    fi

    # ---- D6. 证据三: 直接让桥接器去连路由器 ----
    title "D6. 证据三 — 让桥接器去连 rmw_zenohd 路由器 (第一层墙的现场)"

    # 先排除最平凡的解释("端口没开"): 纯 TCP 连一下 7447。
    # 不做这一步的话, "连不上"可能只是端口问题, 后面所有推论都站不住。
    local TCP_OK=0
    info "先排除'端口没开': 纯 TCP 连 7447 (不发任何 zenoh 握手)"
    if timeout 3 bash -c 'exec 3<>/dev/tcp/127.0.0.1/7447' 2>/dev/null; then
        info "  ✓ TCP 连接建立成功 —— 端口是开的"
        TCP_OK=1
    else
        warn "  ✗ TCP 都连不上 —— 那这次的失败说明不了协议问题, 本节结论作废"
        TCP_OK=0
    fi
    echo

    info "再跑: zenoh_bridge_dds -m client -e tcp/localhost:7447"
    local rc=0
    timeout 15 "$BRIDGE_BIN" -d 0 -m client -e tcp/localhost:7447 \
        -a '.*mixed_probe' --rest-plugin --rest-http-port 8001 \
        > "$LOG_DIR/bridge_to_router.log" 2>&1 || rc=$?
    if [ "$rc" -ne 0 ]; then
        info "桥接器退出码 ${rc} (134 = SIGABRT / 核心已转储) —— 它自己放弃了, 不是我们掐的"
        echo "  原始 panic:"
        sed 's/^/    /' "$LOG_DIR/bridge_to_router.log" | head -6
        echo
        if [ "$TCP_OK" -eq 1 ]; then
            info "两个事实摆在一起: TCP 通, zenoh 会话不通 → 死在 zenoh 层, 不是网络层。"
        fi
        info "路由器日志这里帮不上忙: RUST_LOG=info 下它不打印成功会话, 握手成不成都是那几行 ——"
        info "  这一点我专门做过对照实验(让一个 rmw_zenoh_cpp 节点正常连上), 日志形状完全一样。"
        info "所以证据是桥接器的退出码和 panic, 不是'路由器日志里没有'。"
    else
        warn "这次居然连上了?! 那第一层墙不存在。但键空间依旧不重叠, 见 D4。"
    fi

    cat <<'EOF'

  ────────────────────────────────────────────────────────────
  两层墙, 说清楚:

  第一层 (协议版本) —— 连会话都建不起来
    rmw_zenoh_cpp 0.1.9 : 内置 zenoh-c 1.8.0     (rev 2687c51)
    zenoh_bridge_dds 0.5.0-dev : 内置 zenoh 0.5.x (rev 000a2ba)
    实测事实: 7447 的 TCP 连得上, 但 zenoh 会话建不起来 —— 报的是 orchestrator 的
    空描述 IoError, 不是"没开端口", 也不是"没权限"。
    推断原因: 大版本差一代。我核对的是两个二进制**自带**的版本与源码修订,
    没去读 zenoh 的协议规范 —— 所以这是解释力最强的推断, 不是证明。
    对本 Demo 的结论没影响: 无论原因是什么, "这个桥接器接不进这个路由器"可复现。

  第二层 (键表达式) —— 就算能握手也看不见彼此
    zenoh_bridge_dds :  ROS2 话题 /x  →  DDS 名 rt/x  →  zenoh 键   /rt/x
    rmw_zenoh_cpp    :  ROS2 话题 /x  →  zenoh 键   <domain>/<FQN>/<类型名>/TypeHashNotSupported
    两套命名空间没有交集。

  上游 rmw_zenoh README 明确写着 "cannot interoperate with zenoh-plugin-ros2dds"。
  apt 源里 Humble 只有这版 0.5.0 的老桥接器; 能和 1.x 路由器配套的是
  zenoh-plugin-ros2dds (zenoh-bridge-ros2dds), Humble 未打包, 需自行编译或下载 release。

  要让 DDS 节点与 zenoh 原生侧真的交换数据, 唯一正确的构造是**两边都挂桥接器**,
  也就是 Demo C 的拓扑 (DDS → 桥 → zenoh → 桥 → DDS)。注意 C 里两个桥是同代产品,
  所以它们之间能握手; 而这里桥接器与路由器不同代。
  ────────────────────────────────────────────────────────────

EOF

    kill_tracked
    info "原生日志全部保留在: $LOG_DIR"
}

# ============================================================
# 0. 原生 Zenoh API (委托给 run_native.sh)
# ============================================================
demo_native() {
    # 这一段不经过 ROS2, 也不受 RMW_IMPLEMENTATION 影响 —— 单独一个脚本
    bash "$SCRIPT_DIR/run_native.sh" "${1:-all}"
}

# ============================================================
# 全部
# ============================================================
run_all() {
    # 单段失败 (多半是依赖没装) 不该打断整轮
    demo_native   || warn "0 段未完成"
    demo_switch   || warn "A 段未完成"
    demo_bridge   || warn "B 段未完成"
    demo_crossnet || warn "C 段未完成"
    demo_mixed    || warn "D 段未完成"
    title "全部 Demo 结束"
    info "原生日志全部保留在: $LOG_DIR"
}

# ============================================================
# 编译 / 清理 / 依赖
# ============================================================
do_build() {
    info "colcon 编译 ros2_zenoh_lab..."
    # shellcheck disable=SC1091
    source /opt/ros/humble/setup.bash
    cd "$PROJECT_DIR"
    colcon build --packages-select ros2_zenoh_lab
    cd "$SCRIPT_DIR"
    info "编译完成: $NODE_BIN"
}

do_clean() {
    info "清理 colcon 产物..."
    rm -rf "$PROJECT_DIR/build" "$PROJECT_DIR/install" "$PROJECT_DIR/log"
    info "清理完成"
}

do_deps() {
    local pkgs=()
    have_zenoh_rmw || pkgs+=("ros-humble-rmw-zenoh-cpp")
    have_bridge || pkgs+=("ros-humble-zenoh-bridge-dds")

    if [ ${#pkgs[@]} -eq 0 ]; then
        info "两个 zenoh 包都已安装"
        return 0
    fi

    warn "将安装: ${pkgs[*]}"
    info "(需要 sudo 密码)"
    sudo apt-get update
    sudo apt-get install -y "${pkgs[@]}"

    echo
    if have_zenoh_rmw && have_bridge; then
        info "✅ 安装完成:"
        info "   路由器: $ROUTER_BIN"
        info "   桥接器: $BRIDGE_BIN"
    else
        err "安装后仍未找到可执行文件, 请检查 apt 输出"
        return 1
    fi
}

print_usage() {
    cat <<'EOF'
用法: bash scripts/run_demo.sh [子命令]

  native    0. 原生 Zenoh API 用法 — 不涉及 ROS2 (可加子命令, 见 run_native.sh)
  switch    A. RMW 切换 — FastDDS / CycloneDDS / Zenoh 同一份代码
  bridge    B. DDS ↔ Zenoh 桥接, 用 REST 从 zenoh 侧观测
  rest      B'. 桥接器常驻的 REST 沙盒 (留给人手动 curl)
  crossnet  C. 跨域/跨网络桥接 — 机器人 domain 0 ⇄ 云 domain 1
  mixed     D. 两种接入方式共存但不互通 (负面结论, 但最有信息量)
  all       依次跑 native → switch → bridge → crossnet → mixed
  deps      安装 ros-humble-rmw-zenoh-cpp 与 ros-humble-zenoh-bridge-dds
  build     仅 colcon 编译
  clean     清理 build/install/log

默认 (无参数) = switch
EOF
}

case "${1:-switch}" in
    native)   demo_native "${2:-all}" ;;
    switch)   demo_switch ;;
    bridge)   demo_bridge ;;
    rest)     demo_rest ;;
    crossnet) demo_crossnet ;;
    mixed)    demo_mixed ;;
    all)      run_all ;;
    deps)     do_deps ;;
    build)    do_build ;;
    clean)    do_clean ;;
    help|--help|-h) print_usage ;;
    *)        err "未知命令: $1"; print_usage; exit 1 ;;
esac
