#!/usr/bin/env bash
# ============================================================
#  make_bundle.sh — 在 Ubuntu 侧打一个 "盒子离线包"
#
#  盒子 (H618 / Armbian / Debian 12) 往往拉不动 GitHub:
#  这个脚本在 Ubuntu 上把 aarch64 版 zenoh-c、zenoh-cpp 头文件、
#  以及 native_lab 源码打成一个 tar.gz, scp 过去就能编译。
#
#  用法:
#    bash zenoh/cross_host/make_bundle.sh [输出文件]
#
#  依赖 (Ubuntu 侧):
#    - curl / unzip
#    - ros-humble-rmw-zenoh-cpp (提供 zenoh.hxx, 版本与 libzenohc 一致)
#
#  产物布局 (解压后):
#    zenoh-c-1.8.0-aarch64-unknown-linux-gnu/{include,lib}
#    cross_host/{py_pub.py,py_sub.py,py_get.py}
#    native_lab/{CMakeLists.txt,src/}
# ============================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ZENOH_DIR="$(dirname "$SCRIPT_DIR")"
OUT="${1:-zenoh-cross-host-1.8.0-aarch64.tar.gz}"

ZENOH_VERSION="${ZENOH_VERSION:-1.8.0}"
ARCH="${ARCH:-aarch64-unknown-linux-gnu}"
ZENOH_PREFIX="${ZENOH_PREFIX:-/opt/ros/humble/opt/zenoh_cpp_vendor}"

PKG="zenoh-c-${ZENOH_VERSION}-${ARCH}"
URL="https://github.com/eclipse-zenoh/zenoh-c/releases/download/${ZENOH_VERSION}/zenoh-c-${ZENOH_VERSION}-${ARCH}-standalone.zip"

die() { echo "ERR: $*" >&2; exit 1; }

command -v curl  >/dev/null || die "缺少 curl"
command -v unzip >/dev/null || die "缺少 unzip"
[ -f "$ZENOH_PREFIX/include/zenoh.hxx" ] || \
    die "找不到 $ZENOH_PREFIX/include/zenoh.hxx (先装 ros-humble-rmw-zenoh-cpp)"

WORK="$(mktemp -d /tmp/zenoh_bundle.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

echo "[1/4] 下载 zenoh-c ${ZENOH_VERSION} (${ARCH})..."
curl -fsSL --retry 3 -o "$WORK/zenoh-c.zip" "$URL" || die "下载失败: $URL"

echo "[2/4] 解压..."
mkdir -p "$WORK/$PKG"
unzip -q "$WORK/zenoh-c.zip" -d "$WORK/$PKG"
[ -f "$WORK/$PKG/lib/libzenohc.so" ] || die "包里没有 libzenohc.so"

# zenoh-cpp 是 header-only, 但 aarch64 release 只带 C 头文件;
# 从本机的 zenoh_cpp_vendor 补 zenoh.hxx 和 zenoh/ 目录。
echo "[3/4] 补齐 zenoh-cpp 头文件..."
cp -a "$ZENOH_PREFIX/include/." "$WORK/$PKG/include/"

echo "[4/4] 加入 native_lab 与跨机脚本..."
cp -a "$ZENOH_DIR/native_lab" "$WORK/native_lab"
rm -rf "$WORK/native_lab/build"
cp -a "$SCRIPT_DIR" "$WORK/cross_host"

tar -C "$WORK" -czf "$OUT" "$PKG" native_lab cross_host
BASENAME="$(basename "$OUT")"
echo
echo "完成: $OUT ($(du -h "$OUT" | cut -f1))"
echo "拷到盒子:"
echo "  scp $OUT root@<盒子IP>:/root/"
echo "  scp zenoh/cross_host/setup_box.sh root@<盒子IP>:/root/"
echo "在盒子上:"
echo "  bash /root/setup_box.sh --bundle /root/$BASENAME"
echo "  # 或者解开 bundle 后用里面的脚本:"
echo "  mkdir -p /root/zb && tar xzf /root/$BASENAME -C /root/zb"
echo "  bash /root/zb/cross_host/setup_box.sh --bundle /root/$BASENAME"
