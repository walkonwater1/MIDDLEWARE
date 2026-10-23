#!/usr/bin/env bash
# ============================================================
#  run_native.sh — Zenoh 原生 API 用法 Demo (不涉及 ROS2)
#
#  与 run_demo.sh 的区别:
#    run_demo.sh   : 把 ROS2 接到 Zenoh 上 (换 RMW / 挂桥接器)
#    本脚本        : 直接用 zenoh-cpp 写程序, 演示 Zenoh 本身的用法
#
#  用法:
#    bash scripts/run_native.sh              # 默认 = all
#    bash scripts/run_native.sh pubsub       # 1. 发布/订阅 (peer 模式, 无需路由器)
#    bash scripts/run_native.sh query        # 2. Query/Queryable + GET 的空结果对照
#    bash scripts/run_native.sh router       # 3. client 模式主动连路由器
#    bash scripts/run_native.sh observe      # 4. 原生程序看见 ROS2 的真实 zenoh 键
#    bash scripts/run_native.sh all          # 依次跑 1→2→3→4
#    bash scripts/run_native.sh build        # 只编译 native_lab
#    bash scripts/run_native.sh clean        # 清理 native_lab/build
#
#  observe 需要先编译 ROS2 包 (bash scripts/run_demo.sh build)。
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
NATIVE_DIR="$PROJECT_DIR/native_lab"
NATIVE_BUILD="$NATIVE_DIR/build"
NODE_BIN="$PROJECT_DIR/install/ros2_zenoh_lab/lib/ros2_zenoh_lab/rmw_probe_node"
ROUTER_BIN="/opt/ros/humble/lib/rmw_zenoh_cpp/rmw_zenohd"
ZENOH_PREFIX="${ZENOH_PREFIX:-/opt/ros/humble/opt/zenoh_cpp_vendor}"

LOG_DIR="$(mktemp -d /tmp/zenoh_native.XXXXXX)"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
err()   { echo -e "${RED}[ERR]${NC}  $*"; }
title() { echo -e "\n${CYAN}━━━ $* ━━━${NC}\n"; }

# ============================================================
#  进程管理
# ============================================================
KILL_PIDS=()

cleanup() {
    local pid
    for pid in "${KILL_PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    sleep 0.3
    for pid in "${KILL_PIDS[@]}"; do
        kill -9 "$pid" 2>/dev/null || true
    done
    for pid in "${KILL_PIDS[@]}"; do
        wait "$pid" 2>/dev/null || true
    done
    KILL_PIDS=()
}
trap cleanup EXIT INT TERM

track() { KILL_PIDS+=("$1"); }

# ============================================================
#  编译
# ============================================================
do_build() {
    if [ ! -f "$ZENOH_PREFIX/lib/libzenohc.so" ]; then
        err "找不到 $ZENOH_PREFIX/lib/libzenohc.so"
        err "先装: sudo apt-get install -y ros-humble-rmw-zenoh-cpp"
        return 1
    fi
    info "编译 native_lab (zenoh-c: $ZENOH_PREFIX)..."
    cmake -S "$NATIVE_DIR" -B "$NATIVE_BUILD" \
        -DCMAKE_BUILD_TYPE=Release -DZENOH_PREFIX="$ZENOH_PREFIX" >"$LOG_DIR/cmake.log" 2>&1 || {
        err "cmake 配置失败, 日志: $LOG_DIR/cmake.log"
        tail -20 "$LOG_DIR/cmake.log"
        return 1
    }
    cmake --build "$NATIVE_BUILD" -j"$(nproc)" || return 1
    info "编译完成: $NATIVE_BUILD/{z_pub,z_sub,z_queryable,z_get,z_sysmon}"
}

ensure_built() {
    if [ ! -x "$NATIVE_BUILD/z_pub" ]; then
        do_build || return 1
        return 0
    fi
    # 源码比产物新就增量重编, 免得改了 .cpp 还在跑旧的二进制
    if [ -n "$(find "$NATIVE_DIR/src" "$NATIVE_DIR/CMakeLists.txt" \
            -newer "$NATIVE_BUILD/z_pub" -print -quit 2>/dev/null)" ]; then
        info "检测到源码更新, 重新编译..."
        do_build || return 1
    fi
}

do_clean() {
    info "清理 $NATIVE_BUILD ..."
    rm -rf "$NATIVE_BUILD"
    info "清理完成"
}

start_router() {
    if ss -ltn 2>/dev/null | grep -q ':7447'; then
        warn "7447 上已经有进程在监听 (可能是上一轮残留的路由器)"
    fi
    "$ROUTER_BIN" >"$LOG_DIR/router.log" 2>&1 &
    local pid=$!
    track "$pid"
    local i
    for i in $(seq 1 20); do
        if ss -ltn 2>/dev/null | grep -q ':7447'; then
            info "zenoh 路由器就绪 (pid=$pid, tcp/…:7447)"
            return 0
        fi
        sleep 0.5
    done
    warn "10 秒没等到 :7447, 见 $LOG_DIR/router.log"
}

# ============================================================
#  1. 发布 / 订阅 — Zenoh 的 "hello world"
# ============================================================
demo_pubsub() {
    ensure_built || return 1

    title "N1. 发布 / 订阅 — peer 模式, 不需要路由器"
    info "两个进程都用默认配置: 组播 scouting 自动发现对端, 零配置。"
    echo "  代码: native_lab/src/z_pub.cpp  (Config → open → declare_publisher → put)"
    echo "        native_lab/src/z_sub.cpp  (Config → open → declare_subscriber → 回调)"
    echo

    "$NATIVE_BUILD/z_sub" demo/hello 8 >"$LOG_DIR/n1_sub.log" 2>&1 &
    local sub_pid=$!
    track "$sub_pid"
    sleep 1

    "$NATIVE_BUILD/z_pub" demo/hello 5 15 || true

    wait "$sub_pid" 2>/dev/null || true
    cleanup
    echo
    cat "$LOG_DIR/n1_sub.log"
}

# ============================================================
#  2. Query / Queryable
# ============================================================
demo_query() {
    ensure_built || return 1

    title "N2. Query / Queryable — 请求-响应, 以及 GET 为什么会返回空"
    echo "  代码: native_lab/src/z_queryable.cpp (declare_queryable → query.reply)"
    echo "        native_lab/src/z_get.cpp        (get → 收集有限条 reply)"
    echo

    "$NATIVE_BUILD/z_queryable" 'demo/robot/**' 10 >"$LOG_DIR/n2_q.log" 2>&1 &
    track "$!"
    sleep 1

    info "① 问一个有人应答的 key:"
    "$NATIVE_BUILD/z_get" 'demo/robot/**' 2000 || true
    echo

    info "② 负对照 —— 同一个网络里, 对一个只有发布者、没有 queryable 的 key 做 GET:"
    "$NATIVE_BUILD/z_pub" demo/plain 10 20 >"$LOG_DIR/n2_pub.log" 2>&1 &
    track "$!"
    sleep 1
    "$NATIVE_BUILD/z_get" demo/plain 1500 || true
    echo
    info "   0 条不是故障: GET 是查询, 只命中 Queryable, 不读取 pub/sub 的数据。"
    info "   这正是 zenoh/README.md 里 'curl /rt/** 为空属正常' 的同一件事。"

    cleanup
    echo
    info "queryable 侧日志:"
    cat "$LOG_DIR/n2_q.log"
}

# ============================================================
#  3. client 模式 — 主动连接路由器
# ============================================================
demo_router() {
    ensure_built || return 1
    if [ ! -x "$ROUTER_BIN" ]; then
        warn "缺少 $ROUTER_BIN, 跳过本段"
        return 1
    fi

    title "N3. client 模式 — 主动连路由器 (跨网段 / 穿 NAT 的基础)"
    info "用 ROS2 自带的 rmw_zenohd 当路由器 —— 它就是标准的 Zenoh router (1.8.0)。"
    info "发布端与订阅端都显式写 tcp/localhost:7447, 关掉组播, 只走这条 TCP。"
    echo "  真实部署把 localhost 换成对端 IP 即可; 这一步 DDS 组播做不到。"
    echo

    start_router
    echo

    "$NATIVE_BUILD/z_sub" demo/router 8 tcp/localhost:7447 >"$LOG_DIR/n3_sub.log" 2>&1 &
    local sub_pid=$!
    track "$sub_pid"
    sleep 2

    "$NATIVE_BUILD/z_pub" demo/router 5 10 tcp/localhost:7447 || true

    wait "$sub_pid" 2>/dev/null || true
    cleanup
    echo
    cat "$LOG_DIR/n3_sub.log"
}

# ============================================================
#  4. 原生程序看见 ROS2 的 zenoh 键
# ============================================================
demo_observe() {
    ensure_built || return 1

    if [ ! -x "$ROUTER_BIN" ]; then
        warn "缺少 $ROUTER_BIN, 跳过本段"
        return 1
    fi
    if [ ! -x "$NODE_BIN" ]; then
        warn "ROS2 包还没编译 —— 先跑: bash scripts/run_demo.sh build"
        return 1
    fi

    title "N4. 同一张 zenoh 网络 — 原生程序看得到 ROS2 的键"
    info "起一个 rmw_zenoh_cpp 的 ROS2 发布者 (domain 42, topic /native_probe),"
    info "再让一个原生 z_sub 订阅 '**'。这里能直接看到 rmw_zenoh 的真实键名,"
    info "也能看到载荷是 CDR 编码 (开头 4 字节为 00 01 00 00)。"
    echo "  这也解释了 README 里 '墙二': /native_probe 在 zenoh 上并不叫 /native_probe。"
    echo

    # shellcheck disable=SC1091
    source /opt/ros/humble/setup.bash
    # shellcheck disable=SC1091
    source "$PROJECT_DIR/install/setup.bash"

    start_router
    echo

    env RMW_IMPLEMENTATION=rmw_zenoh_cpp ROS_DOMAIN_ID=42 \
        ZENOH_ROUTER_CHECK_ATTEMPTS=10 \
        ZENOH_LAB_MODE=pub ZENOH_LAB_TOPIC=/native_probe ZENOH_LAB_LABEL=ros2_pub \
        "$NODE_BIN" --ros-args -p mode:=pub -p topic:=/native_probe \
        -p rate_hz:=2.0 -p exit_after:=12.0 >"$LOG_DIR/n4_ros2.log" 2>&1 &
    track "$!"
    sleep 4

    "$NATIVE_BUILD/z_sub" '**' 6 tcp/localhost:7447 >"$LOG_DIR/n4_obs.log" 2>&1 || true

    info "ROS2 发布者自述:"
    grep -E 'RMW 实现|传输 |发布中' "$LOG_DIR/n4_ros2.log" | head -3 || true
    echo
    info "原生订阅端看到的样本 (只显示前 8 条):"
    grep '^\[SUB\]' "$LOG_DIR/n4_obs.log" | head -8 || true
    echo
    info "本次采样中出现过的 key:"
    grep '^\[SUB\]' "$LOG_DIR/n4_obs.log" | grep -o 'key=[^ ]*' | sort -u | sed 's/^/    /' || true
    echo
    info "结论: 原生程序能和 ROS2 共用同一张 zenoh 网络, 但 ROS2 的键是"
    info "      <domain>/<话题>/<类型>/TypeHashNotSupported —— 和 zenoh_bridge_dds 的 /rt/x 没有交集。"

    cleanup
}

# ============================================================
#  all
# ============================================================
run_all() {
    demo_pubsub   || warn "N1 未完成"
    demo_query    || warn "N2 未完成"
    demo_router   || warn "N3 未完成"
    demo_observe  || warn "N4 未完成"
    title "原生 Zenoh Demo 结束"
    info "原生日志保留在: $LOG_DIR"
}

print_usage() {
    cat <<'EOF'
用法: bash scripts/run_native.sh [子命令]

  pubsub   N1. 发布/订阅 — peer 模式, 无需路由器 (Zenoh 的 hello world)
  query    N2. Query/Queryable, 并用负对照解释 GET 为什么可能为空
  router   N3. client 模式主动连 rmw_zenohd 路由器
  observe  N4. 原生订阅 '**', 直接看 ROS2 的 zenoh 键与 CDR 载荷
  all      依次跑 N1 → N2 → N3 → N4 (默认)
  build    仅编译 native_lab
  clean    清理 native_lab/build

原生示例源码: zenoh/native_lab/src/
EOF
}

case "${1:-all}" in
    pubsub)  demo_pubsub ;;
    query)   demo_query ;;
    router)  demo_router ;;
    observe) demo_observe ;;
    all)     run_all ;;
    build)   do_build ;;
    clean)   do_clean ;;
    help|--help|-h) print_usage ;;
    *)       err "未知命令: $1"; print_usage; exit 1 ;;
esac
