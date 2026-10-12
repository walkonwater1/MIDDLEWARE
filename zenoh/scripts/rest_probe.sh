#!/usr/bin/env bash
# ============================================================
#  rest_probe.sh — 从 zenoh 侧观测 ROS2 图的 REST 探针
#
#  背景:
#    zenoh_bridge_dds 自带 REST 插件 (--rest-plugin), 它把 zenoh 键空间
#    暴露成 HTTP 接口。本机没有 zenoh 命令行客户端 (没有 zenohd / zenoh-c /
#    也没有 pip), 所以 curl 是唯一能"站到 zenoh 那边看一眼"的工具。
#
#  用法:
#    bash scripts/rest_probe.sh <PORT> [TOPIC] [SSE_SECONDS]
#    bash scripts/rest_probe.sh 8000 zenoh_probe 8
#
#  打四枪:
#    1. 管理空间 route/**       — 桥接器登记的转发路由 (证据: /rt/<topic>)
#    2. 管理空间 participant/** — 桥接器看到的 DDS 参与者与读写者
#    3. GET /rt/**              — 普通 zenoh 查询 (很可能为空, 见下方说明)
#    4. SSE  /rt/<topic>        — 订阅真实数据流 (主证据)
#
#  为什么第 3 枪可能是空的:
#    桥接器不存储 pub/sub 样本。普通 GET 在 zenoh 里是一次 **查询** (query),
#    只会命中 queryable。所以 GET 返回空不代表桥接失败 —— 第 1 枪的路由表
#    和第 4 枪的 SSE 才是真正的主证据。
# ============================================================

set -e

PORT="${1:-8000}"
TOPIC="${2:-zenoh_probe}"
SSE_SECONDS="${3:-8}"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
err()   { echo -e "${RED}[ERR]${NC}  $*"; }
title() { echo -e "\n${CYAN}━━━ $* ━━━${NC}"; }

BASE="http://localhost:${PORT}"

# ------------------------------------------------------------
# 0. 探活: REST 插件到底起没起
# ------------------------------------------------------------
title "0. REST 插件探活 (${BASE})"
if curl -sg --max-time 3 "${BASE}/@/version" 2>/dev/null; then
    echo
    info "REST 插件在线 (zplugin-rest 的 /@/version 在本版本返回空数组, 能应答即算在线)"
else
    err "连不上 ${BASE} —— 桥接器没起, 或 --rest-plugin 没开?"
    exit 1
fi

# ------------------------------------------------------------
# 1. 管理空间: 转发路由 (最能说明"桥接真的生效了")
# ------------------------------------------------------------
title "1. 管理空间 route/** — 桥接器登记的转发路由"
echo "  $ curl -sg '${BASE}/@/service/**/route/**'"
ROUTES="$(curl -sg --max-time 5 "${BASE}/@/service/**/route/**" 2>/dev/null || true)"
if [ -n "$ROUTES" ]; then
    echo "$ROUTES"
    echo
    info "上面应能看到 route/from_dds//rt/${TOPIC} (或 to_dds/...)"
else
    warn "路由表为空 —— DDS 侧可能还没有发布者/订阅者被发现"
fi

# ------------------------------------------------------------
# 2. 管理空间: 参与者
# ------------------------------------------------------------
title "2. 管理空间 participant/** — 桥接器看到的 DDS 参与者"
echo "  $ curl -sg '${BASE}/@/service/**/participant/**'"
PARTICIPANTS="$(curl -sg --max-time 5 "${BASE}/@/service/**/participant/**" 2>/dev/null || true)"
if [ -n "$PARTICIPANTS" ]; then
    # 列出的是本机 DDS 域里**全部**参与者 (含 rosout/parameter_events/服务对,
    # 甚至别人的 realsense 话题), 动辄几十条。只给前 12 条 + 总数, 免得刷屏。
    PCOUNT="$(printf '%s' "$PARTICIPANTS" | grep -c '"key":' || true)"
    echo "$PARTICIPANTS" | head -12
    if [ "${PCOUNT:-0}" -gt 12 ]; then
        echo "  ... (共 ${PCOUNT} 条, 此处只显示前 12 条)"
    fi
else
    warn "未看到参与者"
fi

# ------------------------------------------------------------
# 3. 普通 GET (zenoh 查询) —— 很可能为空, 属正常
# ------------------------------------------------------------
title "3. GET /rt/** — 普通 zenoh 查询 (为空属正常, 见脚本头说明)"
echo "  $ curl -sg '${BASE}/rt/**'"
GETRESULT="$(curl -sg --max-time 5 "${BASE}/rt/**" 2>/dev/null || true)"
if [ -n "$GETRESULT" ]; then
    echo "$GETRESULT"
else
    echo "  (空)"
    info "返回空是预期行为: GET 是查询而非订阅, 桥接器不存 pub/sub 样本"
    info "→ 请以第 1 枪的路由表和第 4 枪的 SSE 为主证据"
fi

# ------------------------------------------------------------
# 4. SSE 订阅 —— 主证据
# ------------------------------------------------------------
title "4. SSE 订阅 /rt/${TOPIC} — 采样 ${SSE_SECONDS} 秒实时数据"
echo "  $ curl -sgN -H 'Accept: text/event-stream' '${BASE}/rt/${TOPIC}'"
echo "  (上游有 10 秒无数据即断流的守卫, 所以发布端必须持续发)"
echo
# 采样 40 行就收手: 看得见数据即可, 不必把整个流刷屏。
# tr -d '\0' 是必须的: DDS 载荷是 CDR 二进制, 里面有空字节, 直接让 bash 捕获
# 会打印 "命令替换：忽略输入中的 null 字节" 的警告。在管道里就剔掉。
SSE_RAW="$(timeout "$SSE_SECONDS" curl -sgN -H 'Accept: text/event-stream' \
    "${BASE}/rt/${TOPIC}" 2>/dev/null | tr -d '\0' | head -n 40 || true)"
SSE_COUNT="$(printf '%s' "$SSE_RAW" | grep -c '^event:PUT' || true)"

if [ "${SSE_COUNT:-0}" -gt 0 ]; then
    printf '%s\n' "$SSE_RAW" | head -n 12
    echo "  ... (共采样到 ${SSE_COUNT}+ 条, 此处只显示前 12 行)"
    echo
    info "✅ 键空间 /rt/${TOPIC} 上有真实数据流 —— 桥接确实生效了"
    info "   注意 value 里能直接看到 DDS 侧的原始载荷 (前几个字节是 CDR 封装头)"
else
    warn "采样窗口内没有样本"
    warn "→ 要么数据没经过桥 (检查 DDS 侧是否真在发), 要么发布端停了"
fi

echo
info "探针结束。若第 1 枪有路由、第 4 枪有数据, 即证明 ROS2 话题已被桥进 zenoh。"
