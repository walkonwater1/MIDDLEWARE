#!/usr/bin/env bash
# ============================================================
#  run_demo.sh — 一键运行 WebSocket 信令 + 数据流 Demo
#
#  用法:
#    bash scripts/run_demo.sh              # 自动编译 + 服务端(后台) + 客户端(前台)
#    bash scripts/run_demo.sh server       # 仅启动服务端
#    bash scripts/run_demo.sh client       # 仅启动客户端
#    bash scripts/run_demo.sh build        # 仅编译
#    bash scripts/run_demo.sh clean        # 清理编译产物
#
#  环境变量:
#    WS_PORT     服务端口 (默认: 9002)
#    WS_HOST     客户端连接地址 (默认: localhost)
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

SERVER_BIN="$BUILD_DIR/ws-server"
CLIENT_BIN="$BUILD_DIR/ws-client"

WS_PORT="${WS_PORT:-9002}"
WS_HOST="${WS_HOST:-localhost}"

# ---- 颜色 ----
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
err()   { echo -e "${RED}[ERR]${NC}  $*"; }
title() { echo -e "\n${CYAN}$*${NC}"; }

# ============================================================
# 检查依赖
# ============================================================
check_deps() {
    local missing=()

    if ! dpkg -s libwebsocketpp-dev &>/dev/null 2>&1; then
        missing+=("libwebsocketpp-dev")
    fi
    if ! dpkg -s libboost-system-dev &>/dev/null 2>&1; then
        missing+=("libboost-system-dev")
    fi

    if [ ${#missing[@]} -gt 0 ]; then
        err "缺少依赖: ${missing[*]}"
        echo ""
        echo "  安装命令:"
        echo "    sudo apt-get install -y ${missing[*]}"
        exit 1
    fi
}

# ============================================================
# 编译
# ============================================================
do_build() {
    info "检查依赖..."
    check_deps

    info "开始编译..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake "$PROJECT_DIR" -DCMAKE_BUILD_TYPE=Debug
    make -j"$(nproc 2>/dev/null || echo 4)"

    if [ ! -x "$SERVER_BIN" ] || [ ! -x "$CLIENT_BIN" ]; then
        err "编译失败! 找不到可执行文件"
        exit 1
    fi
    info "编译完成"
    cd "$PROJECT_DIR"
}

# ============================================================
# 清理
# ============================================================
do_clean() {
    info "清理编译产物..."
    rm -rf "$BUILD_DIR"
    info "清理完成"
}

# ============================================================
# 运行服务端 (前台)
# ============================================================
run_server() {
    if [ ! -x "$SERVER_BIN" ]; then
        warn "未编译, 先编译..."
        do_build
    fi

    title "WebSocket 信令服务 — 服务端"
    echo ""
    echo "  协议: 通用信封 {\"event\":\"...\", \"session_id\":\"...\", ...}"
    echo "  事件: session_init → session_ready → stream_begin → ..."
    echo "  特性: 会话管理、事件路由、数据处理模拟"
    echo ""
    exec "$SERVER_BIN" "$WS_PORT"
}

# ============================================================
# 运行客户端 (前台)
# ============================================================
run_client() {
    if [ ! -x "$CLIENT_BIN" ]; then
        warn "未编译, 先编译..."
        do_build
    fi

    title "WebSocket 信令客户端 — 完整链路演示"
    echo ""
    echo "  链路: WS信令握手 → 数据推流 → 处理结果 → 数据拉流"
    echo ""
    exec "$CLIENT_BIN" "ws://$WS_HOST:$WS_PORT"
}

# ============================================================
# 同时运行 (服务端后台 + 客户端前台)
# ============================================================
run_all() {
    if [ ! -x "$SERVER_BIN" ] || [ ! -x "$CLIENT_BIN" ]; then
        warn "未编译, 先编译..."
        do_build
    fi

    # 清理函数
    cleanup() {
        echo ""
        info "正在停止..."
        if [ -n "$SVC_PID" ] && kill -0 "$SVC_PID" 2>/dev/null; then
            kill "$SVC_PID" 2>/dev/null
            wait "$SVC_PID" 2>/dev/null || true
        fi
    }
    trap cleanup EXIT INT TERM

    # 启动服务端 (后台)
    info "启动 WebSocket 信令服务 (后台, port=$WS_PORT)..."
    "$SERVER_BIN" "$WS_PORT" &
    SVC_PID=$!
    sleep 1

    if ! kill -0 "$SVC_PID" 2>/dev/null; then
        err "服务端启动失败!"
        exit 1
    fi
    info "服务端已启动 (pid=$SVC_PID)"

    # 启动客户端 (前台)
    echo ""
    info "启动 WebSocket 客户端 (前台, Ctrl+C 退出)..."
    echo ""
    "$CLIENT_BIN" "ws://$WS_HOST:$WS_PORT"

    cleanup
}

# ============================================================
# 用法
# ============================================================
print_usage() {
    echo "用法: bash scripts/run_demo.sh [COMMAND]"
    echo ""
    echo "Commands:"
    echo "  (无)      自动编译 + 服务端(后台) + 客户端(前台)"
    echo "  server    仅启动服务端"
    echo "  client    仅启动客户端"
    echo "  build     仅编译"
    echo "  clean     清理编译产物"
    echo "  help      显示此帮助"
    echo ""
    echo "环境变量:"
    echo "  WS_PORT     服务端口 (默认: 9002)"
    echo "  WS_HOST     客户端连接地址 (默认: localhost)"
    echo ""
    echo "示例:"
    echo "  bash scripts/run_demo.sh                 # 自动运行"
    echo "  bash scripts/run_demo.sh server          # 仅服务端"
    echo "  WS_PORT=8080 bash scripts/run_demo.sh    # 自定义端口"
    echo ""
    echo "协议说明:"
    echo "  所有消息使用通用信封格式，事件名已脱敏处理。"
    echo "  详见 README.md 中的协议文档。"
}

# ============================================================
# Main
# ============================================================
cd "$PROJECT_DIR"

case "${1:-}" in
    server)
        run_server
        ;;
    client)
        run_client
        ;;
    build)
        do_build
        ;;
    clean)
        do_clean
        ;;
    help|--help|-h)
        print_usage
        ;;
    "")
        run_all
        ;;
    *)
        err "未知命令: $1"
        print_usage
        exit 1
        ;;
esac
