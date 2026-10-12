# Zenoh — ROS2 的另一种传输 (DDS ⇄ Zenoh 切换与桥接)

本目录把 **Eclipse Zenoh** 接到 ROS2 上,演示两件 DDS 做不到的事:

1. **换掉整条传输栈** — `RMW_IMPLEMENTATION=rmw_zenoh_cpp`,同一份节点代码,底层不再是 DDS。
2. **跨网络桥接** — `zenoh_bridge_dds` 把整张 ROS2/DDS 图映射进 zenoh 网络,而 zenoh 能在 NAT、网段、5G 之间互联,靠 UDP 组播的 DDS 不行。

---

## 为什么需要 Zenoh?

ROS2 的默认传输是 DDS。DDS 用 **UDP 组播**做发现 (SPDP/SEDP),这在同一个交换机下的机器人内部非常好用 —— 零配置、自适应、无中心节点。但它有一道硬墙:

| 场景 | DDS 组播 | Zenoh |
|------|---------|-------|
| 同一网段内的机器人内部通信 | ✅ 完美 | ✅ |
| 跨网段 / 跨 VLAN | ❌ 组播不转发 | ✅ 一条 TCP/UDP 即可 |
| 穿过 NAT / 防火墙 | ❌ 需要专门打洞或代理 | ✅ 主动连接可达的对端 |
| 5G / 车联网 (公网) | ❌ 基本不可用 | ✅ |
| 多机器人 (车队) 跨云汇聚 | ❌ 域一多就散 | ✅ 路由器负责汇聚 |

于是形成了两种工程范式:

```
范式一 (换栈):    ROS2 节点 ──── rmw_zenoh_cpp ──── zenoh 网络
                  "我全都要跑 Zenoh"

范式二 (桥接):    ROS2 节点 ──── DDS ──── zenoh_bridge_dds ──── zenoh 网络
                  "DDS 我不动, 只把边界那一段桥出去"
```

本目录的四个 Demo 就分别覆盖这两种范式,以及它们**混用时会踩的坑**。

## Zenoh 核心概念

| 概念 | 说明 | DDS 对应物 |
|------|------|-----------|
| **Key Expression** | 层级化的键,如 `rt/cmd_vel`、`robot/1/telemetry` | Topic 名 |
| **Session** | 一个进程与 zenoh 网络的连接 | DomainParticipant |
| **Peer** | 平级的 zenoh 节点,互相直连 | — (DDS 本身就是全 P2P) |
| **Router** | 负责发现与转发的中心节点 (`rmw_zenohd`) | 无 (DDS 无中心节点) |
| **Client** | 只连路由器、不接受别人连入的轻量 peer | — |
| **Query / Queryable** | 请求-响应式的 `get`,可在键空间上做检索 | Service (但模型不同) |
| **Scouting** | 发现对端的方式 (组播 / gossip) | SPDP 组播发现 |
| **Zenoh 管理空间** | 用 `@/...` 前缀暴露运行时信息,可被查询 | 无 |

关键区别:Zenoh 的键是**任意的层级字符串**,而 DDS 的 topic 名有固化规范。**这个差别正是后面 Demo D "不可互通"的根因。**

## ROS2 集成 Zenoh 的三种方式

| 方式 | 怎么做 | 适用 | 本目录 |
|------|--------|------|--------|
| **换 RMW** | `RMW_IMPLEMENTATION=rmw_zenoh_cpp` + 起 `rmw_zenohd` 路由器 | 新建系统 / 全栈可改 | Demo A、D |
| **桥接** | 跑 `zenoh_bridge_dds`,DDS 侧零改动 | 已有 ROS2 系统,只需打通边界 | Demo B、C |
| **原生 API** | 直接链接 zenoh-c/zenoh-cpp 写非 ROS2 程序 | 云端服务、嵌入式端 | 不在本目录 |

---

## ⚠️ 关键前提:两种方式**不可互通**(隔着两层墙)

这是本目录最重要的一条结论,也是很多人第一次接触 Zenoh 时会踩的坑。

> **`rmw_zenoh_cpp` 与 `zenoh_bridge_dds` 都跑在 Zenoh 上,但它们既说不上话,也看不见彼此的键 —— 两张 ROS2 图不会因为共用 zenoh 而合并。**

**墙一:协议版本对不上。** 在 ROS2 Humble 上,这两个包内置的 zenoh 差了一整个大版本
(Humble 里 `rmw_zenoh_cpp` 0.1.9 走 zenoh-c **1.8.0**,而 `zenoh_bridge_dds` 0.5.0
是 zenoh **0.5.x** 那一代的老代码)。实测让桥接器去连 `rmw_zenohd` 的路由器,
它会直接 SIGABRT 核心转储 —— 连 zenoh 会话都建不起来,更谈不上交换数据。

**墙二:键表达式命名空间没有交集。** 就算跨过墙一,同一个 ROS2 话题在两边的键也完全不同:

```
同一个 ROS2 话题 /x,两套完全不同的键:

  zenoh_bridge_dds :  /x → (DDS 名 rt/x) →  zenoh 键   /rt/x
  rmw_zenoh_cpp    :  /x →                zenoh 键   <domain>/<FQN>/<类型名>/TypeHashNotSupported
```

上游 `rmw_zenoh` 的 README 里写得很直白:**"cannot interoperate with `zenoh-plugin-ros2dds`"**。

**推论**:如果你要让一个 DDS 节点和一个 zenoh 原生端交换数据,**唯一正确的构造是两边都挂桥接器**:

```
[ROS2/DDS 节点] ── 桥1 ── zenoh 网络 ── 桥2 ── [ROS2/DDS 节点]
```

也就是 Demo C 的拓扑(两个桥同级同代,所以能互通)。Demo D 会把这两层墙当场演示出来。

---

## 项目结构

```
zenoh/
├── README.md                       # 本文档
├── ros2_zenoh_lab/                 # ROS2 包 (★ zenoh/ 是一个独立 colcon 工作区)
│   └── src/rmw_probe_node.cpp        RMW 无关的自省节点 (Demo A–D 全靠它)
├── scripts/
│   ├── run_demo.sh                 一键编排: switch | bridge | rest | crossnet | mixed | all
│   ├── rest_probe.sh               从 zenoh 侧观测的 REST/SSE 探针
├── config/
│   ├── router_tcp_ipv4.json5       IPv4-only 主机的路由器配置 (回落)
│   └── session_remote.json5        跨机连远端路由器的会话模板
└── build/ install/ log/            colcon 产物 (已 gitignore)
```

## 快速开始

```bash
# 0. 安装两个 apt 包 (本目录不编译它们, 只是运行期依赖)
bash zenoh/scripts/run_demo.sh deps

# 1. 编译本目录的 ROS2 包 (zenoh/ 是独立工作区, 不影响 ../ros2/)
cd zenoh && source /opt/ros/humble/setup.bash && colcon build --packages-select ros2_zenoh_lab

# 2. 依次跑四个 Demo
bash zenoh/scripts/run_demo.sh all
```

单个 Demo:

```bash
bash zenoh/scripts/run_demo.sh switch     # A. RMW 切换
bash zenoh/scripts/run_demo.sh bridge     # B. DDS ↔ Zenoh 桥接
bash zenoh/scripts/run_demo.sh crossnet   # C. 跨域桥接
bash zenoh/scripts/run_demo.sh mixed      # D. 共存但不互通
```

> ⚠️ 每次运行请在**干净的 shell** 里,或先 `unset RMW_IMPLEMENTATION ROS_DOMAIN_ID`。
> 脚本内部会自己清一遍,但你手敲命令时不会。

---

## Demo A — RMW 切换:同一份代码,三条传输栈

> **本文档里所有 Demo 输出都是从本机真实运行里粘贴的**,不是示意。凡是单次运行会
> 变动的字段(条数、等待时间、GUID/zid),文中都注明了波动范围或"某一次运行"。
> 这些 Demo 在 2026-10-12 的 ROS2 Humble 上跑过完整四段,退出码 0、无进程残留。


`rmw_probe_node` 是 RMW 无关的:它只调 `rmw_get_implementation_identifier()`,不链接任何 DDS/Zenoh 库。换传输完全靠环境变量。

```bash
# A1. FastDDS (Humble 默认)
RMW_IMPLEMENTATION=rmw_fastrtps_cpp ros2 run ros2_zenoh_lab rmw_probe_node --ros-args -p mode:=pub

# A2. CycloneDDS (换 DDS 供应商, 仍是 RTPS)
RMW_IMPLEMENTATION=rmw_cyclonedds_cpp ros2 run ros2_zenoh_lab rmw_probe_node --ros-args -p mode:=sub

# A3. Zenoh (换掉整个传输栈 — 需先起路由器)
ros2 run rmw_zenoh_cpp rmw_zenohd &
RMW_IMPLEMENTATION=rmw_zenoh_cpp ros2 run ros2_zenoh_lab rmw_probe_node --ros-args -p mode:=pub
```

节点启动即自报传输拓扑:

```
│ RMW 实现    : rmw_fastrtps_cpp
│ 传输        : eProsima Fast DDS — 发现: DDS 组播 SPDP/SEDP
│ ROS_DOMAIN_ID      : 10
```

切到 Zenoh 后,同一个节点会多打印一节 Zenoh 会话信息:

```
│ RMW 实现    : rmw_zenoh_cpp
│ 传输        : Eclipse Zenoh (非 DDS 传输) — 发现: Zenoh router gossip 会话
│ ─────────── Zenoh 会话 ───────────
│ ZENOH_SESSION_CONFIG_URI  : (未设置)
│ ZENOH_ROUTER_CHECK_ATTEMPTS : 10
```

A3 段(zenoh)实测输出 —— 订阅端退出时打印的 `SUMMARY`:

```
[INFO] [1791792486.414746421] [rmw_probe_node]: 收到 #1: {"rmw":"rmw_zenoh_cpp","label":"zenoh_pub","seq":6,"ts":1791792486414127702,"domain":"12","host":"lixin-ERAZER"}
[INFO] [1791792492.919466442] [rmw_probe_node]: SUMMARY mode=sub topic=/zenoh_probe sent=0 received=14 peer_rmw=rmw_zenoh_cpp rmw=rmw_zenoh_cpp domain=12
```

注意 `peer_rmw=rmw_zenoh_cpp` —— 载荷里写着**对端的 RMW 实现**。同样的节点、
同样的话题名、同样的代码,只是 `RMW_IMPLEMENTATION` 变了,对端就从 `rmw_fastrtps_cpp`
变成了 `rmw_zenoh_cpp`。这一行就是"整条传输栈被换掉"的直接证据。

### 四段对比(实测)

各段用**不同的 `ROS_DOMAIN_ID`**(10/11/12/13)保证互不串台。"首条等待"是订阅端
自己量的(从启动到收到第一条);"收到"是这一段跑完时的总数。下面是**两次**完整
`run_demo.sh all` 的数字,写成区间而非单值 —— 单跑一次的数字不稳定,别当基准:

| 段 | RMW | 发现方式 | 首条等待 | 收到 | 结论 |
|----|-----|---------|---------|------|------|
| A1 | `rmw_fastrtps_cpp` | DDS 组播 SPDP/SEDP | 0.2 s | 12 条 | 基线 |
| A2 | `rmw_cyclonedds_cpp` | DDS 组播 SPDP/SEDP | 0.5 s | 12 条 | 换供应商,零代码改动 |
| A3 | `rmw_zenoh_cpp` | Zenoh router gossip | 0.0–0.5 s | 14 条 | **换了整条栈**(不再是 DDS) |
| A4 | FastDDS 发 → CycloneDDS 收 | DDS 组播 | **3.0–3.5 s** | 10–11 条 | **跨供应商互通**(RTPS 线协议) |

两点如实的观察,不额外归因:

- **A4 反而是四段里最慢的**(3.0–3.5 s,高于 A1/A2/A3)。同一台机器上跨供应商的
  SPDP/SEDP 匹配比同供应商慢 —— 但只跑了两次、没控制变量(发布端启动时机、
  机器负载都没固定),所以只能算**现象记录**,不能当结论。要下定论得重复多次。
- **收到条数 10~14 不等,没有一段是 0**。这里看的是"0 还是非 0":负对照必须是 0,
  正结果只要明显非 0 就成立。别把条数差解读成丢包率 —— 它主要反映发布端
  已经跑了多久。

A4 是**正对照**,它存在的意义是让 Demo D 的负结果有意义:
**DDS 家族内部能互通,是因为它们共用 RTPS 线协议;Zenoh 不在此列,因为键空间不同。**

---

## Demo B — DDS ↔ Zenoh 桥接(用 REST 从 zenoh 侧观测)

DDS 侧不动,只加一个桥接器,ROS2 话题就出现在 zenoh 键空间里。

```bash
# 终端1: DDS 侧发布者
ROS_DOMAIN_ID=0 ros2 run ros2_zenoh_lab rmw_probe_node --ros-args -p mode:=pub -p topic:=/zenoh_probe

# 终端2: 桥接器 (DDS domain 0 → zenoh), 带 REST 插件
/opt/ros/humble/lib/zenoh_bridge_dds/zenoh_bridge_dds \
  -d 0 -l tcp/localhost:7447 --rest-plugin --rest-http-port 8000 -a '.*zenoh_probe'

# 终端3: 从 zenoh 侧看
bash zenoh/scripts/rest_probe.sh 8000 zenoh_probe 8
```

**负对照先行**(桥接器还没起,zenoh 侧应该什么都看不到):

```
━━━ B1. 负对照 — 桥接器还没起, zenoh 侧什么都看不到 ━━━
[INFO]  curl http://localhost:8000/@/version → 连接失败 ✓
[INFO]  也就是说: ROS2 话题此刻只活在 DDS 世界里, zenoh 网络上不存在
```

**起桥接器后,四枪探针的实测输出**(挑主证据):

```
━━━ 1. 管理空间 route/** — 桥接器登记的转发路由 ━━━
[
{ "key": "/@/service/CA2A4061E732460194A571D295B255F3/dds/route/from_dds/rt/zenoh_probe", "value": {"routed_writers":["010f51fe1cc466580000000000001203"]}, ... }
]

━━━ 3. GET /rt/** — 普通 zenoh 查询 (为空属正常, 见脚本头说明) ━━━
[

]

━━━ 4. SSE 订阅 /rt/zenoh_probe — 采样 8 秒实时数据 ━━━
event:PUT
data:{ "key": "/rt/zenoh_probe", "value": "<CDR头>{"rmw":"rmw_fastrtps_cpp","label":"dds_talker","seq":35,"ts":1791792512867290390,"domain":"0","host":"lixin-ERAZER"}", "encoding": "application/octet-stream", "time": "..." }

event:PUT
data:{ "key": "/rt/zenoh_probe", "value": "<CDR头>{"rmw":"rmw_fastrtps_cpp","label":"dds_talker","seq":36,...}", ... }
...
  ... (共采样到 14+ 条, 此处只显示前 12 行)

[INFO]  ✅ 键空间 /rt/zenoh_probe 上有真实数据流 —— 桥接确实生效了
[INFO]     注意 value 里能直接看到 DDS 侧的原始载荷 (前几个字节是 CDR 封装头)
```

三件事被这一屏同时证明:

1. **`route/from_dds/rt/zenoh_probe`** 存在,且 `routed_writers` 里挂着 DDS 侧的
   writer GUID —— 桥接器**主动登记**了这条转发路由,不是碰巧。
2. **第 3 枪是空的,而这不是失败**。普通 `GET` 在 zenoh 里是一次**查询**(query),
   只有 queryable 会应答;桥接器不存储 pub/sub 样本,所以查询返回 `[ ]`。
   把 GET 当"订阅"用是常见误解 —— 见下面的专门小节。
3. **SSE 是主证据**。`event:PUT` 里 `value` 字段就是 DDS 侧的原始载荷:
   前面几个不可打印字节是 **CDR 封装头**(`\x00\x01\x00\x00`),后面才是
   `rmw_probe_node` 发的 JSON 字符串。也就是说 —— **桥接器没有做任何转译**,
   它把 DDS 的字节流原样搬到了 zenoh 键上。这也解释了 `encoding` 为什么是
   `application/octet-stream` 而不是 `application/json`。

### 桥接器命令里每个参数的含义

| 参数 | 含义 |
|------|------|
| `-d 0` | DDS domain ID。**必须显式传** —— 这个版本的桥接器不读 `ROS_DOMAIN_ID` |
| `-l tcp/localhost:7447` | zenoh 侧监听的地址 |
| `--rest-plugin --rest-http-port 8000` | 开 REST 插件,把 zenoh 键空间暴露成 HTTP |
| `-a '.*zenoh_probe'` | **白名单**:只桥接匹配的话题 |
| `-m peer` (默认) / `client` | zenoh 会话模式 |

`-a` 强烈建议一定要加:不加的话 `/rosout`、`/parameter_events`、`/tf` 会被全部桥过去,流量和噪声都会失控。

### 为什么普通 `GET /rt/**` 可能是空的

桥接器**不存储** pub/sub 样本。在 zenoh 里普通 GET 是一次 **查询 (query)**,只会命中 queryable。所以:

- ❌ `curl http://localhost:8000/rt/**` → 很可能返回空,**这不代表桥接失败**
- ✅ `curl .../@/service/**/route/**` → 桥接器登记的转发路由,**这才是"桥接生效"的硬证据**
- ✅ `curl -N -H 'Accept: text/event-stream' .../rt/zenoh_probe` → SSE 订阅,**这是数据真的流过来的硬证据**

（上游 SSE 有个 10 秒无数据即断流的守卫,所以发布端必须持续发。)

---

## Demo C — 跨域 / 跨网络桥接(机器人 ⇄ 云)

这是 Zenoh 相比原生 DDS **最有价值**的场景。

```
[机器人: DDS domain 0] --pub--> |桥1| ==zenoh==> |桥2| --sub--> [云: DDS domain 1]
       /fleet/telemetry                                              /fleet/telemetry
```

两个 DDS 域各自为政,谁也不知道对方存在 —— 这正是 DDS 组播的死结。中间用 zenoh 把两个域接起来:

```bash
# 机器人侧桥接器
zenoh_bridge_dds -d 0 -l tcp/localhost:7447 --rest-plugin --rest-http-port 8000 -a '.*telemetry'

# 云侧桥接器 (显式 -e 连到机器人侧, -l 换端口避免冲突)
zenoh_bridge_dds -d 1 -l tcp/localhost:7448 -e tcp/localhost:7447 \
  --rest-plugin --rest-http-port 8001 -a '.*telemetry'
```

在真实部署里,`tcp/localhost:7447` 换成 `tcp/<机器人IP>:7447`,就跨了网段;换成公网地址,就跨了 NAT/DDS 想都不敢想的距离。

**负对照先行**(起桥之前,两个 DDS 域老死不相往来):

```
[INFO]  ↑ 后台启动 [rmw_fastrtps_cpp @ domain 0] pid=50362
[INFO]  ↑ 后台启动 [rmw_fastrtps_cpp @ domain 1] pid=50363
[INFO]  云侧 (domain 1) 已收 0 条
[INFO]  ✓ domain 1 收不到 domain 0 的组播 —— 这就是 DDS 跨域/跨网的死结
```

**起两个桥接器之后**:

```
[INFO]  ↑ 桥接器 DDS domain 0 → zenoh (mode=peer, REST=:8000, pid=50421)
[INFO]  ↑ 桥接器 DDS domain 1 → zenoh (mode=peer, REST=:8001, pid=50501)
[INFO]  云侧 (domain 1) 现在收到 34 条
[INFO]  ✓ domain 0 的消息穿过 zenoh, 落到了 domain 1
```

从 **0 条** 到 **34 条**,中间只多了两个桥接器 —— DDS 侧的节点代码一行没改,
`ROS_DOMAIN_ID` 也没改。这就是"跨域/跨网"的实质。

桥接器管理空间里的路由证据 —— **两个桥接器看到的是同一份路由表**:

```
桥1 (domain 0, REST :8000):
[
{ "key": ".../CF9014573B8C435182AE30E88B75FEE6/dds/route/from_dds/rt/fleet/telemetry", "value": {"routed_writers":["010f51fe63d2c6e0…1203"]}, ... },
{ "key": ".../15CC33F49C554F459A2C1F123E390EDF/dds/route/to_dds/rt/fleet/telemetry",   "value": {"routed_readers":["010f51fe64d26ef3…1204"]}, ... }
]

桥2 (domain 1, REST :8001):
[
{ "key": ".../CF9014573B8C435182AE30E88B75FEE6/dds/route/from_dds/rt/fleet/telemetry", "value": {"routed_writers":["010f51fe63d2c6e0…1203"]}, ... },
{ "key": ".../15CC33F49C554F459A2C1F123E390EDF/dds/route/to_dds/rt/fleet/telemetry",   "value": {"routed_readers":["010f51fe64d26ef3…1204"]}, ... }
]
```

这点值得多看一眼:**两个桥接器上的路由表一模一样**,连 zid 都相同
(`CF9014…` 是桥1 的,`15CC33…` 是桥2 的)。原因是两个桥接器连到同一个 zenoh 网络后
会**互相联邦** —— zenoh 的路由信息是网络范围内共享的,不像 DDS 那样每个域各看各的。
`from_dds` 那条带着 domain 0 的 writer GUID,`to_dds` 那条带着 domain 1 的 reader GUID:
一条注册"我在往 zenoh 里送",另一条注册"我在从 zenoh 里收",两个方向都齐了。

> 上面这些数字来自**一次**运行。重跑会变:两次运行里 C 段"云侧收到"分别是 34 和 29,
> A 段的"首条等待"也在 0.0–3.5 s 之间浮动。**这些字段看的是"0 还是非 0"**,
> 而不是精确值 —— 负对照必须是 0,正结果只要明显非 0 就成立。

**顺序很重要**:订阅者必须先存在,桥接器的 `to_dds` 路由才会建立 —— 因为路由是响应"发现了 DDS 订阅"而创建的,不是预先配置的。Demo C 里两个桥接器同代(都是 0.5.0),
所以它们之间能正常握手 —— 这一点在 Demo D 会形成鲜明对照。

### 真的跨机:换成 IP 就好

上面 `tcp/localhost:7447` 换成机器人/边缘网关的 IP,就跨了网段:

```bash
# 机器人侧网关 —— 对外监听, 这里用 0.0.0.0 让同网的机器能连进来
zenoh_bridge_dds -d 0 -l tcp/0.0.0.0:7447 --rest-http-port 8000 -a '.*telemetry'

# 云侧 —— 主动连过去 (NAT 后面也能连, 这正是 Zenoh 相对 DDS 组播的核心优势)
zenoh_bridge_dds -d 1 -e tcp/<机器人IP>:7447 --rest-http-port 8001 -a '.*telemetry'
```

如果你走的是**换 RMW** 那条路,节点侧连远端路由器用 `config/session_remote.json5`:

```bash
export ZENOH_SESSION_CONFIG_URI=<绝对路径>/config/session_remote.json5   # 改里面的 IP
export RMW_IMPLEMENTATION=rmw_zenoh_cpp
ros2 run <你的包> <你的节点>
```

该文件关掉了组播 scouting(`scouting.multicast.enabled: false`)—— 跨网段时组播本来就不通,
留着只会做无用功。**这份配置的 schema 已实测被 zenoh 1.8.0 采纳**(验证方法见文件头注释:
把 connect 端点换成非默认端口, 确认节点真的照着去连, 而不是回落到 7447)。

---

## Demo D — 两种接入方式共存,但**不互通**

本 Demo 的结论是负面的,但它是全目录信息量最大的一节。

一行命令跑完整段:

```bash
bash scripts/run_demo.sh mixed
```

它做的事:**同一台机器、同一个 domain、同一个话题名**,两边各起一个节点 ——

```bash
# 走 zenoh 的发布者 (rmw_zenoh_cpp, 经路由器)
RMW_IMPLEMENTATION=rmw_zenoh_cpp ROS_DOMAIN_ID=0 ZENOH_ROUTER_CHECK_ATTEMPTS=10 \
  ros2 run ros2_zenoh_lab rmw_probe_node \
  --ros-args -p mode:=pub -p topic:=/mixed_probe -p label:=zenoh_native

# 走 DDS 的订阅者 (rmw_fastrtps_cpp)
RMW_IMPLEMENTATION=rmw_fastrtps_cpp ROS_DOMAIN_ID=0 \
  ros2 run ros2_zenoh_lab rmw_probe_node \
  --ros-args -p mode:=sub -p topic:=/mixed_probe -p label:=dds_peer
```

直觉上应该能通:消息都在,zenoh 网络也在。**实测结果是:一层都过不去,而且是两层墙。**

#### 墙一:协议版本 —— 连 zenoh 会话都建不起来

让桥接器以 `client` 模式去连 `rmw_zenohd` 路由器。先说清楚**不是端口的问题** ——
路由器在听的时候,纯 TCP 连 7447 是通的:

```
$ ss -ltn | grep :7447
LISTEN 0      1024               *:7447             *:*
$ timeout 3 bash -c 'exec 3<>/dev/tcp/127.0.0.1/7447'   # 纯 TCP, 不发 zenoh 握手
  ✓ TCP 连接建立成功 → 端口是开的, 死在 zenoh 层
```

然后再让桥接器去接:

```
[INFO]  跑: zenoh_bridge_dds -m client -e tcp/localhost:7447
[INFO]  桥接器退出码 134 (134 = SIGABRT / 核心已转储) —— 它自己放弃了, 不是我们掐的
  原始 panic:
    ERROR zenoh::net::runtime::orchestrator] Unable to connect to any of [Locator { address: Tcp(DnsName("localhost:7447")), metadata: None }]!
    thread 'main' panicked at src/main.rs:121:10:
    called `Result::unwrap()` on an `Err` value: ZError { kind: IoError { descr: "" },
      file: "/home/buildfarm/.cargo/git/checkouts/zenoh-cc237f2570fab813/000a2ba/zenoh/src/net/runtime/orchestrator.rs", line: 130, source: None }
[INFO]  两个事实摆在一起: TCP 通, zenoh 会话不通 → 死在 zenoh 层, 不是网络层。
```

`descr: ""` —— 一个**空描述**的 `IoError`,来自 orchestrator(负责建链路的那个模块)。
TCP 通、会话不通,于是问题只能出在 zenoh 自己的握手/协议层。**注意别用"路由器日志里
没有这条会话"当证据** —— 我做过对照实验:让一个 `rmw_zenoh_cpp` 节点正常连上,
路由器在 `RUST_LOG=info` 下打印的日志形状**完全一样**,它压根不记录成功会话。
所以证据是桥接器的退出码与 panic,不是日志的缺席。

那为什么握不上?最符合现有证据的解释是版本:

| 组件 | 自报版本 | 内置 zenoh 源码修订 |
|------|---------|-------------------|
| `rmw_zenoh_cpp` 0.1.9(路由器侧) | zenoh-c **1.8.0** | `zenoh-cc237f2570fab813/**2687c51**` |
| `zenoh_bridge_dds` 0.5.0 | `zenoh bridge for DDS **v0.5.0-dev**` | `zenoh-cc237f2570fab813/**000a2ba**` |

**同一个仓库的两个 git 修订,大版本从 0.5 跨到 1.x。**

> ⚠️ **这一步是推断,不是证明。** 我核对的是两个二进制里**自带的**版本与源码修订,
> 以及"TCP 通、会话不通"这个事实;我**没有**去读 zenoh 的 wire protocol 规范,
> 也没找到上游一句明确写"0.5 客户端不能连 1.x 路由器"的话(查过一次,没查到可信来源)。
> 所以严格地说:**这两代产品在本机上握不上手**是实测事实,**原因是版本差一代**
> 是解释力最强的推断。对本目录的结论没有影响 —— 无论原因是什么,
> "这个桥接器接不进这个路由器"是确定可复现的。

> 版本号怎么取?`scripts/run_demo.sh` 里的 `zenoh_ver_vendor` / `zenoh_rev_router` /
> `zenoh_rev_bridge` 三个函数全部从**真实产物**里挖(zenoh-c 的 CMake
> `ConfigVersion.cmake`、`libzenohc.so` 与桥接器二进制里的 cargo checkout 路径),
> 没有一个是写死的。`rmw_zenohd` 本身只有 47 KB,是 `libzenohc.so` 的薄壳,
> 修订串在 `.so` 里而不在它自己身上。

#### 墙二:键表达式 —— 就算能握手也看不见彼此

把桥接器改成 `peer` 模式独立跑(不碰路由器,这样它自己完全正常:
`✅ 桥接器活着, REST 插件在线`),再来观测同一个话题名:

- zenoh 侧的管理空间里**只有** `/rt/mixed_probe`(DDS 那一套键)
- 键 `/rt/mixed_probe` 上**一条数据都没有** —— 因为唯一在发 `/mixed_probe` 的节点
  走的是另一套键,根本没经过桥。桥这一侧自然是死的
- DDS 侧的 `dds_peer` 收到 **0** 条

```
  zenoh_bridge_dds :  /mixed_probe →  zenoh 键  /rt/mixed_probe
  rmw_zenoh_cpp    :  /mixed_probe →  zenoh 键  <domain>/mixed_probe/<类型名>/TypeHashNotSupported
                                              ↑ 没有交集
```

#### 结论

**"接入同一个 zenoh 网络" ≠ "并进同一张 ROS2 图"。**

- 上游 `rmw_zenoh` 明确声明
  [*"Consequently, `rmw_zenoh` cannot interoperate with `zenoh-plugin-ros2dds`."*](https://github.com/ros2/rmw_zenoh/blob/humble/README.md)
  —— 注意上游把原因归结为**键表达式**差异(就是本节"墙二"),
  "墙一"那道协议版本的坎是本机实测额外碰到的;
- apt 源里 Humble 只有这版 0.5.0 的**老**桥接器;能和 1.x 路由器配套的是
  `zenoh-plugin-ros2dds`(`zenoh-bridge-ros2dds`),**Humble 未打包**,需自行编译或下载 release;
- Demo C 里两个桥接器能互通,是因为它们**同代**;这里桥接器与路由器**不同代**。

**要真的通,两边都得挂桥接器**(Demo C 的拓扑:边沿各挂一个同级桥接器,
让 zenoh 网络负责中段)。别指望"换 RMW"和"挂桥接"这两条路在半路汇合 ——
它们各有各的键空间,而且当前 Humble 上放的还是两代 zenoh。

---

## 环境变量参考

从 `librmw_zenoh_cpp.so` 里提取的实际变量名:

| 变量 | 作用 |
|------|------|
| `ZENOH_SESSION_CONFIG_URI` | 节点会话配置的**绝对路径**(连哪个路由器) |
| `ZENOH_ROUTER_CONFIG_URI` | 路由器配置的绝对路径(`rmw_zenohd` 读) |
| `ZENOH_CONFIG_OVERRIDE` | 用 `key=value` 形式覆盖单个配置项,如 `scouting/multicast/enabled=true` |
| `ZENOH_ROUTER_CHECK_ATTEMPTS` | 启动时检查路由器的重试次数,见下表 |
| `ZENOH_SHM_ALLOC_SIZE` | Zenoh 共享内存池大小 |
| `ZENOH_SHM_MESSAGE_SIZE_THRESHOLD` | 超过该体积的消息走共享内存 |
| `RMW_ZENOH_BUFFER_POOL_MAX_SIZE_BYTES` | RMW 缓冲池上限 |

`ZENOH_ROUTER_CHECK_ATTEMPTS` 的语义容易误解,单独说:

| 取值 | 行为 |
|------|------|
| 未设置 | 默认尝试 **1** 次 |
| `0` | **无限**重试 |
| `< 0` (如 `-1`) | **跳过检查** —— 不强制要求路由器存在 |
| `> 0` | 重试该次数,间隔 1 秒 |

注意:检查失败**不会**让初始化失败,只是不阻塞。真正的坑是:0.1.9 自带的路由器配置里 **session 的组播 scouting 是关着的**,发现全靠 router gossip。所以:

```bash
# 常规做法: 起路由器
ros2 run rmw_zenoh_cpp rmw_zenohd
export RMW_IMPLEMENTATION=rmw_zenoh_cpp

# 无路由器 (peer 直连) 的配方 —— 两个条件缺一不可
export ZENOH_ROUTER_CHECK_ATTEMPTS=-1
export ZENOH_CONFIG_OVERRIDE='scouting/multicast/enabled=true'
```

---

## 排错表

| 现象 | 原因 | 处理 |
|------|------|------|
| `ros2 node list` 显示的图不对/是旧的 | 切 RMW 后 ros2 daemon 还是上一个 RMW 的 | `ros2 daemon stop && pkill -9 -f 'ros2 daemon'`,脚本已内置 |
| 路由器起不来,报 `Address family not supported` | 主机没有 IPv6,而默认配置监听 `[::]:7447` | `ZENOH_ROUTER_CONFIG_URI=<绝对路径>/config/router_tcp_ipv4.json5`。⚠️ 本机有 IPv6,**这个失败没能复现**,属预期场景;该配置文件本身已实测可加载 |
| `curl /rt/**` 返回空 | 普通 GET 是查询,不是订阅 | 属正常;看 `@/service/**/route/**` 和 SSE |
| 桥接器起了,但 `route` 表是空的 | 桥接器没看到 DDS 侧 | 检查 `-d` 是否等于 `ROS_DOMAIN_ID`;多网卡时桥接器静态链接的 CycloneDDS 可能选错网卡 → `CYCLONEDDS_URI` 强制 `lo` |
| 两个桥接器抢 7447 | peer 模式默认监听端口相同 | 各自显式 `-l` 不同端口,如 7447 / 7448 |
| Demo C 里云侧始终 0 条 | 订阅者比桥接器晚起,`to_dds` 路由没建 | 先起订阅者再起桥接器 |
| zenoh 节点报连不上路由器 | 路由器没起,或 `ZENOH_ROUTER_CHECK_ATTEMPTS` 用尽 | 起 `rmw_zenohd`;或 `ZENOH_ROUTER_CHECK_ATTEMPTS=0` 无限重试 |
| `-p` 参数不生效,节点用了默认值 | rmw_zenoh 上 service 调用可能被静默丢弃 | 用 `ZENOH_LAB_MODE` 等环境变量兜底(节点已支持) |
| 桥接器把 `/rosout` `/tf` 全桥过去了 | 没加白名单 | 一定加 `-a '<regex>'` |
| **桥接器 `-m client -e tcp/…:7447` 去连 `rmw_zenohd` 直接 SIGABRT 核心转储** | 两者内置的 zenoh 差一个大版本(Humble 上桥接器 0.5.0 走 zenoh 0.5.x,路由器走 zenoh-c 1.8.0) | **别这么连**。要跨网就用同代桥接器两端对接(Demo C);0.5.0 桥接器与 1.x 路由器无法共存 |
| 桥接器报 `Unable to connect to any of [Locator {…}]`,但端口确实在听 | 同上 —— TCP 通、zenoh 握手不通;报的是 `orchestrator` 的空描述 `IoError` | 同上一行。排查时先 `ss -ltn` + 纯 TCP 连一下,排除端口问题再怀疑协议 |
| 上一轮跑完 `7447` 还被占着 | 上一轮异常中断,`trap` 没跑完 | `pkill -9 -f 'rmw_zenoh[d]'`(注意方括号要**包住名字内的字符**,写成 `rmw_zenohd[x]` 是错的 —— 那样连真进程都匹配不到) |

---

## 与其他中间件对比

| 维度 | DDS (ROS2 默认) | Zenoh |
|------|----------------|-------|
| 定位 | 局域网实时数据分发 | 云边端统一的数据总线 |
| 发现 | UDP 组播 SPDP/SEDP | 组播 scouting / router gossip |
| 跨网段 | ❌ | ✅ |
| 穿 NAT | ❌ | ✅ (主动连接可达对端) |
| 中心节点 | 无 (纯 P2P) | 可选 (router) |
| 键空间 | Topic 有固化规范 | 任意层级字符串 |
| ROS2 接入 | 原生 | 换 RMW 或挂桥接器 |
| 零拷贝 | Loan / SHM | Zenoh SHM (`ZENOH_SHM_*`) |
| 服务模型 | Service / Action | Query/Queryable (模型不同) |

## 参考资料

- [Eclipse Zenoh 官网](https://zenoh.io/)
- [ros2/rmw_zenoh](https://github.com/ros2/rmw_zenoh) — 官方 RMW 实现
- [ros2/rmw_zenoh — humble 分支 README](https://github.com/ros2/rmw_zenoh/blob/humble/README.md)
  —— Demo D 引用的那句话出自这里,原文:
  *"Consequently, `rmw_zenoh` cannot interoperate with `zenoh-plugin-ros2dds`."*
  注意上游把原因归结为**键表达式**不同(Demo D 的"墙二"),"墙一"(协议版本)
  是本机实测到的额外一道 —— 上游没说这件事。
- [zenoh-bridge-dds 文档](https://github.com/eclipse-zenoh/zenoh-plugin-dds) — 本目录 0.5.0 版桥接器的上游
- [zenoh-plugin-ros2dds](https://github.com/eclipse-zenoh/zenoh-plugin-ros2dds) ——
  **新一代**桥接器(`zenoh-bridge-ros2dds`),配套 zenoh 1.x 路由器。
  ROS2 Humble **未打包**(apt 里只有老的 `zenoh-bridge-dds`),要用得自己编或下 release
- [Zenoh 键表达式](https://zenoh.io/docs/manual/abstractions/) — 理解 Demo D 不互通的基础
