# 跨主机验证:Ubuntu ⇄ H618 (Armbian)

用一台刷了 Armbian 的 **H618 电视盒子**当对端,验证 Zenoh 在**真实异构主机**
(x86_64 ⇄ aarch64)之间的发布/订阅与查询。这一节不依赖 ROS2,只验证 Zenoh 本身。

## 实测环境

| | Ubuntu (本机) | H618 (盒子) |
|---|---|---|
| 架构 | x86_64 | **aarch64** (4×Cortex-A53) |
| 系统 | Ubuntu 22.04 | Armbian 25.05 bookworm (Debian 12) |
| 内核 | 6.8.0 | 6.12.11-edge-sunxi64 |
| glibc | 2.35 | **2.36** |
| Python | 3.10 | 3.11.2 |
| 内存 / 存储 | — | 3.8 GiB / 114 GB eMMC |
| 地址 | `192.168.10.1/24` (USB 网卡) | `192.168.10.2/24` (end0) |
| Zenoh | **1.8.0** (随 `rmw_zenoh_cpp`) | 系统包 1.10.1 + 本文装的 1.8.0 |
| 链路 | USB 网卡直连,**RTT 1.5–1.9 ms**,无 AP 中转 | |

盒子是 `Armbian-unofficial 25.05.0-trunk`、`BOARD=h618`、`sun50iw9`。
直连意味着没有 WiFi 组播过滤、没有客户端隔离 —— 是验证 Zenoh 最干净的拓扑。

> **版本对齐**:Ubuntu 的 `rmw_zenoh_cpp` 内置 zenoh **1.8.0**,所以盒子上也装
> `eclipse-zenoh==1.8.0` / `zenoh-c 1.8.0`。盒子自带的 1.10.1 是系统包,
> 与本文的 venv / native_lab 互不干扰(各自链接各自的库)。

## 两种拓扑

```
      Ubuntu (x86_64)                              H618 (aarch64)
      192.168.10.1                                 192.168.10.2
           │                                              │
           │            USB 网卡直连 (同一 L2)             │
           │                                              │
   ┌───────┴────────┐        A. router + client   ┌───────┴────────┐
   │ rmw_zenohd     │ ←─────── TCP 7447 ─────────→ │ z_sub (client) │
   │  (1.8.0 路由器) │                              │ py_pub         │
   │ z_pub / z_sub  │                              │ native_lab     │
   └────────────────┘                              └────────────────┘

   B. peer ↔ peer:两端都用默认配置,组播 scouting 自动发现,不需要路由器
```

- **A. router + client**:推荐。跨网段/穿 NAT 用的就是这条形态 —— 两端都**主动**
  连向一个可达的 router。Zenoh 不做 UDP 打洞。
- **B. peer ↔ peer**:直连链路上可以零配置跑通(已实测);换成 WiFi AP 时,
  组播过滤或客户端隔离可能让它失效,那时改用 A。

## 实测结果

| # | 场景 | 发布端 | 订阅端 | 模式 | 结果 |
|---|------|--------|--------|------|------|
| 1 | C++ ⇄ C++ | 盒子 `z_pub` | Ubuntu `z_sub` | peer | **5/5** |
| 2 | C++ ⇄ C++ | Ubuntu `z_pub` | 盒子 `z_sub` | peer | **5/5** |
| 3 | C++ → Python | Ubuntu `z_pub` | 盒子 `py_sub` | router | **5/5** |
| 4 | Python → C++ | 盒子 `py_pub` | Ubuntu `z_sub` | router | **5/5** |
| 5 | 查询 | 盒子 `py_get` | Ubuntu `z_queryable` | peer | **1 条 reply** |
| 6 | 跨小版本路由 | Ubuntu `z_pub` (1.8.0) | Ubuntu `z_sub` (1.8.0),经盒子 `zenohd` **1.10.1** | router | **4/4** |
| 7 | **真实采集上传** | 盒子 `z_sysmon` (CPU/内存/温度) | Ubuntu `z_sub 'h618/**'` | peer | **15/15** (5 个采样点 × 3 key) |

第 6 行说明 1.8 的 client 与 1.10 的 router 能正常建链与路由;但**生产上仍建议
两端锁同一版本**,小版本之间可能改行为或默认值。

## 快速开始

### 1. Ubuntu 侧:准备并打包

```bash
# 编译本仓库的原生示例 (复用 rmw_zenoh_cpp 自带的 zenoh-c 1.8.0)
bash zenoh/scripts/run_native.sh build

# 打 aarch64 离线包 (zenoh-c 1.8.0 + zenoh-cpp 头文件 + native_lab + 本目录脚本)
bash zenoh/cross_host/make_bundle.sh
# → zenoh-cross-host-1.8.0-aarch64.tar.gz (~15 MB)

scp zenoh-cross-host-1.8.0-aarch64.tar.gz root@192.168.10.2:/root/
scp zenoh/cross_host/setup_box.sh          root@192.168.10.2:/root/
```

> 盒子上 **GitHub 和 Eclipse 的下载站拉不动**(实测 curl 超时),但 Debian 源
> 和 PyPI 可达。所以 zenoh-c 的 aarch64 包要在 Ubuntu 侧下好再传;
> `make_bundle.sh` 就是干这个的。

### 2. 盒子侧:安装

```bash
# 只装 Python (在线):
bash /root/setup_box.sh

# 没有外网时,先传 aarch64 wheel,再离线装:
#   eclipse_zenoh-1.8.0-cp39-abi3-manylinux_2_28_aarch64.whl
bash /root/setup_box.sh --wheel /root/wheels/eclipse_zenoh-1.8.0-*.whl

# Python + C++ 一起装:
bash /root/setup_box.sh --bundle /root/zenoh-cross-host-1.8.0-aarch64.tar.gz \
                        --wheel  /root/wheels/eclipse_zenoh-1.8.0-*.whl
```

装完在 `/opt/zenoh-cross-host/`:

```
venv/                 eclipse-zenoh 1.8.0
scripts/              py_pub.py / py_sub.py / py_get.py
cpp/zenoh-c-1.8.0-aarch64-.../   aarch64 libzenohc + 头文件
cpp/native_lab/build/ z_pub / z_sub / z_queryable / z_get / z_sysmon
```

### 3. 跑起来

**A. 路由器模式** — Ubuntu 起 `rmw_zenohd`,盒子作为 client 连过去:

```bash
# Ubuntu 终端 1:路由器 (监听 *:7447)
/opt/ros/humble/lib/rmw_zenoh_cpp/rmw_zenohd

# 盒子:订阅
/opt/zenoh-cross-host/cpp/native_lab/build/z_sub demo/cross 30 tcp/192.168.10.1:7447

# Ubuntu 终端 2:发布
zenoh/native_lab/build/z_pub demo/cross 2 10 tcp/192.168.10.1:7447
```

**B. peer 模式** — 两边都不带 endpoint,组播自动发现:

```bash
# 盒子
/opt/zenoh-cross-host/cpp/native_lab/build/z_sub demo/cross 30

# Ubuntu
zenoh/native_lab/build/z_pub demo/cross 2 10
```

**Python 版**(盒子):

```bash
/opt/zenoh-cross-host/venv/bin/python /opt/zenoh-cross-host/scripts/py_sub.py \
    demo/cross 30 tcp/192.168.10.1:7447

/opt/zenoh-cross-host/venv/bin/python /opt/zenoh-cross-host/scripts/py_pub.py \
    demo/cross 2 10 tcp/192.168.10.1:7447

# 查询:Ubuntu 端先跑 z_queryable 'demo/robot/**' 30
/opt/zenoh-cross-host/venv/bin/python /opt/zenoh-cross-host/scripts/py_get.py \
    'demo/robot/**' 2000 tcp/192.168.10.1:7447
```

## H618 采集端:z_sysmon

前 6 个场景验证的是"能通",这一项开始验证"能用来研究通信":盒子采集自己
**真实的**系统状态,通过 Zenoh 上报;Ubuntu 一侧订阅并做后续统计(延迟、丢包、
对齐)。采集的是内核暴露的标准接口,没有模拟值:

| 数据 | 来源 |
|------|------|
| CPU 使用率 | `/proc/stat` 两次采样的差值(不是瞬时值) |
| load average / 进程数 / uptime | `/proc/loadavg`、`/proc` 数字目录、`/proc/uptime` |
| 内存 | `/proc/meminfo` 的 `MemTotal` / `MemAvailable` |
| 温度 | `/sys/class/thermal/thermal_zone*/{type,temp}`(内核导出千分之一 °C) |

每个采样点发布 3 条消息,**共用同一个 `seq` 与 `ts_ns`**:

```json
h618/system/cpu
{"seq":0,"ts_ns":1792736603290778862,"host":"h618","usage_pct":0.0,"load1":0.35,"load5":0.12,"load15":0.04,"procs":120,"uptime_s":81747}

h618/system/memory
{"seq":0,"ts_ns":1792736603290778862,"host":"h618","total_mb":3919,"available_mb":3654,"used_mb":265,"usage_pct":6.8}

h618/system/temperature
{"seq":0,"ts_ns":1792736603290778862,"host":"h618","max_c":55.8,"zones":[{"type":"cpu-thermal","temp_c":55.8},{"type":"ddr-thermal","temp_c":55.1},{"type":"gpu-thermal","temp_c":55.5},{"type":"ve-thermal","temp_c":54.0}]}
```

> 一次 `z_sysmon` 只在采样时读 `/proc`、`/sys`,1 Hz 下对这块 4×A53 的占用
> 可以忽略;后续要把仿真机器人状态提到 10–50 Hz 时,采集逻辑也应换成
> 增量/缓存,而不是每次重扫目录。

**跑起来**(两边都 peer 模式,先起订阅端并留 5 秒余量):

```bash
# Ubuntu:一次收全三路 key
zenoh/native_lab/build/z_sub 'h618/**' 15

# 盒子:5 个采样点,1 Hz
/opt/zenoh-cross-host/cpp/native_lab/build/z_sysmon h618/system 1 5
```

实测 5 个采样点 × 3 条 = **15/15 全部收到**,`host` 为 `h618`,温度落在
53.6–55.8 °C,内存总量 3919 MB(与 `free -m` 一致)。

**为什么三路分开而不是一个大 JSON**:订阅端可以只关心温度(例如做热控),
也可以 `h618/**` 全收;Zenoh 的 key expression 过滤发生在网络边缘,不需要
应用层再拆包。等做到仿真机器人状态(10–50 Hz)时,这个划分还会继续用。

## 踩过的坑

1. **订阅声明是异步的**。`py_sub`/`z_sub` 打印 "已订阅" 只代表声明完成,
   不代表跨机会话已经建好。实测给 **5 秒**余量才稳定;只等 2–3 秒时第一轮
   收到 0 条。脚本里都是先起订阅端、sleep 5 再起发布端。
2. **盒子有两个 zenoh**。系统 `apt` 装的 1.10.1(`/usr/bin/zenohd`)和我们装的
   1.8.0(`/opt/zenoh-cross-host`)并存。**不要在一个进程里混用**;Python venv 和
   native_lab 各自链接自己的库,系统 zenohd 只在你主动启动时才参与。
3. **Debian 12 不是 ROS2 Humble 的目标平台**。盒子要跑 ROS2 节点,用 arm64
   Docker(`ros:humble` + `rmw_zenoh_cpp`),不要在 Debian 上直接 apt 装
   `ros-humble-*`。盒子已装 Docker 20.10.24。
4. **盒子的默认路由指向 Ubuntu**(`192.168.10.1`),即 Ubuntu 在给它做 NAT。
   实测 Debian 源、PyPI 可达;GitHub 与 Eclipse 的下载站超时 ——
   zenoh-c 的 aarch64 预编译包只能从 Ubuntu 侧下好再传。
5. **multicast 只在同一 L2 有效**。直连时 peer 模式零配置可用;换到 WiFi/跨网段
   就必须用 router 模式(或配置 `scouting/multicast/interface`)。
6. **盒子端不需要安装 ROS2**。本目录的跨机验证只用到 zenoh 本身,Python 或
   native_lab 任一即可。

## 文件

| 文件 | 在哪跑 | 作用 |
|------|--------|------|
| `py_pub.py` / `py_sub.py` / `py_get.py` | 盒子或 Ubuntu | 与 C++ `z_pub`/`z_sub`/`z_get` 等价的 Python 版 |
| `make_bundle.sh` | Ubuntu | 下载 aarch64 zenoh-c、拼头文件、打离线包 |
| `setup_box.sh` | 盒子 | 建 venv、装 1.8.0、解包并编译 native_lab |
