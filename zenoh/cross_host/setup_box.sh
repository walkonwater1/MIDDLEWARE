#!/usr/bin/env bash
# ============================================================
#  setup_box.sh — 在 H618 (Armbian / Debian 12, aarch64) 上装好
#                跨主机验证所需的 Python / C++ 环境
#
#  用法 (在盒子上执行, 需要 root):
#    bash setup_box.sh --wheel /root/wheels/eclipse_zenoh-1.8.0-*.whl
#    bash setup_box.sh --bundle /root/zenoh-cross-host-1.8.0-aarch64.tar.gz
#    bash setup_box.sh --wheel ... --bundle ...    # 两个都装
#
#  选项:
#    --wheel FILE    离线安装 eclipse-zenoh (无外网时用)
#    --bundle FILE   make_bundle.sh 生成的包, 解压并编译 native_lab
#    --prefix DIR    安装目录 (默认 /opt/zenoh-cross-host)
#
#  版本必须与 Ubuntu 侧一致 (默认 1.8.0): Ubuntu 的 rmw_zenoh_cpp
#  内置的就是 zenoh 1.8.0。
# ============================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="/opt/zenoh-cross-host"
WHEEL=""
BUNDLE=""
ZENOH_VERSION="1.8.0"

while [ $# -gt 0 ]; do
    case "$1" in
        --wheel)  WHEEL="${2:?--wheel 需要文件路径}"; shift 2 ;;
        --bundle) BUNDLE="${2:?--bundle 需要文件路径}"; shift 2 ;;
        --prefix) PREFIX="${2:?--prefix 需要目录}"; shift 2 ;;
        -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
        *) echo "未知参数: $1" >&2; exit 1 ;;
    esac
done

die()  { echo "ERR: $*" >&2; exit 1; }
info() { echo "[INFO] $*"; }

[ "$(id -u)" -eq 0 ] || die "请用 root 执行 (会写 $PREFIX)"
command -v python3 >/dev/null || die "缺少 python3"

ARCH="$(uname -m)"
[ "$ARCH" = "aarch64" ] || echo "[WARN] 当前架构是 $ARCH, 本脚本按 aarch64 准备"

mkdir -p "$PREFIX"

# ── 1. Python 运行时 ────────────────────────────────────────
if [ ! -x "$PREFIX/venv/bin/python" ]; then
    info "创建 venv: $PREFIX/venv"
    python3 -m venv "$PREFIX/venv" || die "venv 创建失败, 先装 python3-venv"
fi

if [ -n "$WHEEL" ]; then
    [ -f "$WHEEL" ] || die "找不到 wheel: $WHEEL"
    info "离线安装 eclipse-zenoh==$ZENOH_VERSION"
    "$PREFIX/venv/bin/pip" install -q --no-index \
        --find-links "$(dirname "$WHEEL")" "eclipse-zenoh==$ZENOH_VERSION"
else
    info "在线安装 eclipse-zenoh==$ZENOH_VERSION"
    "$PREFIX/venv/bin/pip" install -q "eclipse-zenoh==$ZENOH_VERSION"
fi

# ── 2. C++ 示例 (可选) ─────────────────────────────────────
if [ -n "$BUNDLE" ]; then
    [ -f "$BUNDLE" ] || die "找不到 bundle: $BUNDLE"
    command -v cmake >/dev/null || die "缺少 cmake (apt install cmake)"
    command -v g++   >/dev/null || die "缺少 g++ (apt install build-essential)"

    info "解压 bundle: $PREFIX/cpp"
    mkdir -p "$PREFIX/cpp"
    tar xzf "$BUNDLE" -C "$PREFIX/cpp"

    ZENOH_C_DIR="$(find "$PREFIX/cpp" -maxdepth 1 -type d -name 'zenoh-c-*' | head -1)"
    [ -n "$ZENOH_C_DIR" ] || die "bundle 里没有 zenoh-c-* 目录"

    info "编译 native_lab (zenoh-c: $ZENOH_C_DIR)"
    cmake -S "$PREFIX/cpp/native_lab" -B "$PREFIX/cpp/native_lab/build" \
        -DCMAKE_BUILD_TYPE=Release -DZENOH_PREFIX="$ZENOH_C_DIR" >/dev/null
    cmake --build "$PREFIX/cpp/native_lab/build" -j"$(nproc)" >/dev/null
    info "C++ 示例: $PREFIX/cpp/native_lab/build/{z_pub,z_sub,z_queryable,z_get,z_sysmon}"
fi

# ── 3. 跨机脚本 ────────────────────────────────────────────
mkdir -p "$PREFIX/scripts"
SRC_SCRIPTS="$SCRIPT_DIR"
if [ ! -f "$SRC_SCRIPTS/py_pub.py" ] && [ -d "$PREFIX/cpp/cross_host" ]; then
    SRC_SCRIPTS="$PREFIX/cpp/cross_host"
fi
if [ -f "$SRC_SCRIPTS/py_pub.py" ]; then
    cp -f "$SRC_SCRIPTS/py_pub.py" "$SRC_SCRIPTS/py_sub.py" \
          "$SRC_SCRIPTS/py_get.py" "$PREFIX/scripts/"
    info "Python 脚本: $PREFIX/scripts/"
fi

# ── 4. 自检 ────────────────────────────────────────────────
info "自检:"
"$PREFIX/venv/bin/python" - <<'PY'
import zenoh
print("  python zenoh: import ok")
PY
echo
echo "用法示例 (Ubuntu 路由器跑在 192.168.10.1:7447):"
echo "  $PREFIX/venv/bin/python $PREFIX/scripts/py_sub.py demo/cross 30 tcp/192.168.10.1:7447"
echo "  $PREFIX/venv/bin/python $PREFIX/scripts/py_pub.py demo/cross 2 10 tcp/192.168.10.1:7447"
echo "  $PREFIX/venv/bin/python $PREFIX/scripts/py_get.py 'demo/robot/**' 2000 tcp/192.168.10.1:7447"
if [ -x "$PREFIX/cpp/native_lab/build/z_pub" ]; then
    echo "  $PREFIX/cpp/native_lab/build/z_sub demo/cross 30 tcp/192.168.10.1:7447"
fi
if [ -x "$PREFIX/cpp/native_lab/build/z_sysmon" ]; then
    echo "  $PREFIX/cpp/native_lab/build/z_sysmon h618/system 1 10   # 盒子上采集真实 CPU/内存/温度"
fi
