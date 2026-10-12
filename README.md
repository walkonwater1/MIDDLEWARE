# 通信中间件学习

**C++17 / C11 / Python | CMake 3.10+ | MIT License**

> 车载与机器人通讯中间件学习仓库 — 12 种中间件覆盖从底层 IPC 到云端 IoT、从 Web 实时通信到云原生 RPC 的完整通信技术栈

## 动机

车载和机器人系统需要多种通信机制各司其职：关节电机通过实时总线同步、ECU 间通过服务发现协商、传感器数据通过零拷贝共享、远程状态通过 MQTT 上报云端。本仓库通过可运行的 Demo 逐一实践每种中间件，理解其设计取舍和适用边界。

## 中间件全景

### 实时总线 (硬实时、确定性)

| 中间件 | 定位 | 确定性 | 典型带宽 |
|--------|------|--------|---------|
| **[EtherCAT](./ethercat/)** | 工业实时以太网，多轴同步 | μs 级 DC 时钟 | 100 Mbps |
| **[CANopen](./canopen/)** | 基于 CAN 的应用层协议 | ms 级 | 1 Mbps |

**EtherCAT** — "总线即中间件"。帧在从站间"飞读飞写"，主站 1kHz 扫描 3 轴伺服，分布式时钟 <100ns 同步。CoE (CANopen over EtherCAT) 让 CANopen 对象字典运行在 EtherCAT 物理层上。**数据报解析器**完整实现 BRD/BWR/LRD/LWR/APRD/FPWR 等全部读写命令，主站构造真实 EtherCAT 数据报经共享内存帧缓冲区与从站交互，含 WKC 验证。

**CANopen** — 机器人关节的"领域语言"。PDO 循环推送电机状态、SDO 读写配置参数、NMT 管理从机生命周期、Heartbeat + EMCY 监控健康。**SDO 协议完整实现** (Upload/Download/Abort 响应帧)，支持 SocketCAN `vcan0` 桥接，可扩展到真实 CAN 硬件。

### 车载 SOA (服务导向)

| 中间件 | 定位 | 通信模式 | 传输层 |
|--------|------|---------|--------|
| **[SOME/IP](./someip/)** | AUTOSAR 车载以太网 | Service Discovery + RPC + Field | UDP/TCP |
| **[DDS](./dds/)** | 去中心化实时数据分发 | Pub/Sub (P2P, 无 Broker) | UDP 组播 |

**SOME/IP** — AUTOSAR Adaptive 平台标准。Service Discovery 动态发现、Method (RPC) 远程调用、Event 事件推送、**Field (Get/Set/Notifier)** 三位一体属性访问。演示车载服务端提供车速/车门事件 + 车窗控制方法 + 空调温度 Field，客户端自动发现并订阅。

**DDS** — OMG 国际标准，ROS2 底层。11 个 Lab 从 Pub/Sub 入门到 QoS/Listener/Security/Benchmark 全覆盖。去中心化无 Broker，SPDP/SEDP 自动发现，20+ QoS 策略组合。

### 云原生 RPC

| 中间件 | 定位 | 通信模式 | 传输层 |
|--------|------|---------|--------|
| **[gRPC](./grpc/)** | 高性能 RPC 框架 | Unary + Client/Server/Bidi Stream | HTTP/2 (TCP) |

**gRPC** — Google 开源的云原生 RPC 标准。IDL (Protobuf) 定义接口 → 自动生成类型安全的 Client/Server 桩代码 → HTTP/2 多路复用 + 双向流。演示 Unary RPC (一问一答)、Server Streaming (遥测推送)、Client Streaming (进度上报) 三种模式，6 轴机械臂状态模拟。

### 机器人中间件

| 中间件 | 定位 | 通信模式 | 语言 |
|--------|------|---------|------|
| **[ROS2](./ros2/)** | 机器人操作系统通信层 | Pub/Sub + Service + Action | C++ (rclcpp) |
| **[Zenoh](./zenoh/)** | ROS2 的非 DDS 传输 | Key-Expression Pub/Sub + Query | C++ (rmw_zenoh / 桥接器) |

**ROS2** — 建立在 DDS 之上的机器人集成框架。15 个 Lab 覆盖 Topic/Service/Action/QoS，延伸到 TF2 坐标变换、Rosbag2 录制回放、Loan Message 零拷贝、多机自动发现、DDS 供应商切换、MQTT 桥接。`dds_bridge` 验证 ROS2 底层即 DDS。

**Zenoh** — 把 ROS2 从局域网里放出来。DDS 靠 UDP 组播发现，跨不了网段、过不了 NAT；Zenoh 只要一条到对端的 TCP 就能互联。两种接入方式：**换 RMW**（`rmw_zenoh_cpp`，整条传输栈不再是 DDS）与**挂桥接**（`zenoh_bridge_dds`，DDS 侧零改动，只把边界那一段桥出去）。四个 Demo 覆盖 RMW 切换、REST 可观测的桥接、跨域机器人↔云链路，以及**两种方式不可互通**这一必须知道的前提 —— Humble 上两者内置的 zenoh 差一个大版本（1.8.0 vs 0.5.x），桥接器连路由器会直接核心转储，且键表达式命名空间也没有交集。

### 进程间通信 (IPC)

| 中间件 | 定位 | 延迟 | 拷贝次数 |
|--------|------|------|---------|
| **[共享内存](./shm/)** | POSIX 共享内存映射 | ~ns 级 | 0 (零拷贝) |
| **[ZeroMQ](./zmq/)** | 高性能异步消息库 | ~μs 级 | 1 |
| **[GDBus](./gdbus/)** | Linux 桌面/嵌入式 IPC | ~μs 级 | 1 |

**共享内存** — IPC 延迟下限。`mmap` 映射同一物理页，A 进程写入 B 进程立即可见。C++17 RAII 封装 (`ShmHandle`/`ShmMapper`/`Shm<T>`)。提供两种无锁队列：**SPSC 环形缓冲区** (load/store, 最高吞吐) 和 **MPMC 有界队列** (CAS + slot turn 计数，支持多生产者多消费者)。

**ZeroMQ** — "不是消息队列，是没有 Broker 的消息库"。**5 种通信模式全覆盖**：REQ/REP 车辆诊断 RPC、PUB/SUB 传感器广播、PUSH/PULL 管道负载均衡、ROUTER/DEALER 异步诊断、PAIR 独占对双向同步。多线程消费者个数可配置。

**GDBus** — Linux 系统总线的 IPC 标准。XML 接口定义 → gdbus-codegen + Python 代码生成 → C++17 RAII 封装。机器人状态管理服务 + 监控终端，方法/信号/属性齐全。

### 车联网 / IoT (云-边-端)

| 中间件 | 定位 | 通信模式 | 传输层 |
|--------|------|---------|--------|
| **[MQTT](./mqtt/)** | IoT 标准消息协议 | Pub/Sub (有 Broker) | TCP |

**MQTT** — 车载终端到云端的桥梁。模拟 5 路 CAN 信号 → Protobuf 序列化 → MQTT 发布，支持 MQTT v5 和 GB/T 32960 国标电动车远程监控协议。aarch64 交叉编译支持车载嵌入式部署。

### Web 实时通信

| 中间件 | 定位 | 通信模式 | 传输层 |
|--------|------|---------|--------|
| **[WebSocket](./websocket/)** | 全双工实时 Web 通信 | 双向 (Text/Binary Frame) | TCP (HTTP Upgrade) |

**WebSocket** — 浏览器到后端唯一的原生双向通道。TCP 连接经 HTTP Upgrade 后自由收发，无需轮询。演示多客户端并发管理、1s 广播遥测 JSON、命令分发 (status/echo/cmd)、Ping/Pong 心跳。适用于机器人的 Web 仪表盘和 HMI 实时监控。

## 选型决策矩阵

| 场景 | 推荐中间件 | 原因 |
|------|-----------|------|
| 机器人关节多轴同步控制 | EtherCAT / CANopen | 硬实时、分布式时钟、确定性延迟 |
| ECU 间服务调用 (车门/车窗) | SOME/IP | Service Discovery、Field Get/Set/Notifier、AUTOSAR 标准 |
| 自动驾驶传感器数据分发 | DDS (ROS2) | 去中心化、QoS 可配、RTPS 实时 |
| 机器人上层算法集成 | ROS2 | TF2/rosbag2/Lifecycle 完整工具链 |
| 激光雷达点云进程间传输 | 共享内存 | 零拷贝、GB/s 级吞吐、ns 级延迟 |
| 多线程任务分发 (负载均衡) | 共享内存 MPMC 队列 | CAS 无锁、多消费者并发 |
| 微服务间异步消息 | ZeroMQ | 无 Broker、5 种灵活拓扑 |
| 并行任务流水线 | ZeroMQ PUSH/PULL | 自动 round-robin 分发 |
| Linux 系统服务管理 | GDBus (D-Bus) | systemd/NetworkManager 原生协议 |
| ECU 间远程方法调用 (诊断/自检) | gRPC | IDL 类型安全、HTTP/2 多路复用、双向流 |
| 机器人 Web 仪表盘实时监控 | WebSocket | 浏览器原生支持、全双工、低开销帧 |
| 车辆远程监控/OTA | MQTT | 穿透防火墙、低带宽、QoS 分级 |
| 机器人跨网段/跨云通信 (NAT/5G) | Zenoh | 主动连接取代组播发现、Key-Expression 命名空间、可穿 NAT |

## 目录结构

```
middleware/
├── ethercat/              # EtherCAT 工业实时以太网 (datagram 处理器 + CoE SDO)
├── canopen/               # CANopen 应用层协议 (SDO完整实现 + SocketCAN 桥接)
├── someip/                # SOME/IP 车载 SOA (Method + Event + Field 三种模式)
├── dds/                   # DDS 去中心化 Pub/Sub (11 Lab)
├── grpc/                  # gRPC 高性能 RPC (3 模式: Unary/Server Stream/Bidi Stream)
├── ros2/                  # ROS2 机器人集成框架 (16 Lab)
├── shm/                   # 共享内存 IPC (SPSC ringbuf + MPMC lockfree queue)
├── zmq/                   # ZeroMQ 高性能消息库 (5 模式: REQ/REP, PUB/SUB, PUSH/PULL, ROUTER/DEALER, PAIR)
├── gdbus/                 # GDBus/D-Bus Linux IPC
├── websocket/             # WebSocket 全双工 Web 通信 (多客户端广播 + 命令分发)
├── mqtt/                  # MQTT IoT 协议 (CAN→Protobuf→MQTT v3/v5/GB32960)
├── zenoh/                 # Eclipse Zenoh 云边总线 (rmw_zenoh + DDS↔Zenoh 桥接)
└── README.md
```

## 学习路线

按通信层次从底层到上层逐步推进：

1. **IPC 基础** → `shm/` (零拷贝 + 无锁队列) → `zmq/` (5 种通信模式) → `gdbus/` (系统 IPC)
2. **实时总线** → `canopen/` (CAN 应用层, 完整 SDO) → `ethercat/` (工业以太网, 数据报协议)
3. **车载 SOA** → `someip/` (AUTOSAR Method/Event/Field) → `dds/` (去中心化 Pub/Sub)
4. **机器人集成** → `ros2/` (DDS 之上的完整框架，16 Lab) → `zenoh/` (把 ROS2 接出局域网: 换 RMW + 跨网桥接)
5. **云边通信** → `mqtt/` (CAN→云端全链路) → `websocket/` (Web 实时遥测)
6. **微服务 RPC** → `grpc/` (IDL+HTTP/2 多模式 RPC)

## 快速运行

大部分 Demo 提供一键脚本：

```bash
# 实时总线
bash ethercat/scripts/run_demo.sh both     # EtherCAT: 1kHz 3轴伺服
bash canopen/scripts/run_demo.sh both       # CANopen: CiA 402 伺服驱动

# 车载 SOA
bash someip/scripts/run_demo.sh both        # SOME/IP: 车辆服务 (Method+Event+Field)

# 进程间通信
bash shm/scripts/run_demo.sh ringbuf        # 共享内存: SPSC 环形缓冲区
bash shm/scripts/run_demo.sh lockfree       # 共享内存: MPMC 无锁队列
bash zmq/scripts/run_demo.sh all            # ZeroMQ: 依次运行全部 5 种模式
bash zmq/scripts/run_demo.sh push_pull      # ZeroMQ: PUSH/PULL 负载均衡

# 机器人 / 云边桥接
bash zenoh/scripts/run_demo.sh all          # Zenoh: A 切换 → B 桥接 → C 跨域 → D 共存
bash zenoh/scripts/run_demo.sh switch       # Zenoh: DDS ⇄ Zenoh 的 RMW 切换
bash zenoh/scripts/run_demo.sh crossnet     # Zenoh: 机器人 domain 0 ⇄ 云 domain 1

# Web / RPC
bash grpc/scripts/run_demo.sh               # gRPC: 3 种 RPC 模式
bash websocket/scripts/run_demo.sh          # WebSocket: 多客户端广播
```

## 构建速查

所有子项目均使用 CMake 构建：

```bash
# EtherCAT         依赖: librt, libpthread
cd ethercat && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# CANopen          依赖: 无外部库 (纯用户态虚拟 CAN 总线)
cd canopen && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# SOME/IP          依赖: libvsomeip3-dev
cd someip && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# DDS              依赖: cyclonedds-dev
cd dds && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# ROS2             依赖: /opt/ros/humble/setup.bash
cd ros2 && source /opt/ros/humble/setup.bash
colcon build --packages-select topic_lab service_lab action_lab qos_lab \
  lifecycle_lab composition_lab param_lab launch_lab tf2_lab rosbag2_lab \
  loan_lab multimachine_lab dds_vendor_lab mqtt_lab dds_bridge

# Zenoh            依赖: /opt/ros/humble/setup.bash + ros-humble-rmw-zenoh-cpp + ros-humble-zenoh-bridge-dds
cd zenoh && source /opt/ros/humble/setup.bash
colcon build --packages-select ros2_zenoh_lab

# 共享内存         依赖: librt
cd shm && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# ZeroMQ           依赖: libzmq3-dev, cppzmq
cd zmq && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# GDBus            依赖: libglib2.0-dev, gdbus-codegen, python3
cd gdbus && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# MQTT             依赖: libpaho-mqtt-dev, protobuf-c-compiler
cd mqtt && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# gRPC             依赖: libgrpc++-dev, protobuf-compiler-grpc
cd grpc && mkdir -p build && cd build && cmake .. && make -j$(nproc)

# WebSocket        依赖: libwebsocketpp-dev, libboost-system-dev
cd websocket && mkdir -p build && cd build && cmake .. && make -j$(nproc)
```

## 通信模式对照

| 模式 | EtherCAT | CANopen | SOME/IP | DDS | gRPC | ZMQ | MQTT | WebSocket |
|------|----------|---------|---------|-----|------|-----|------|-----------|
| Pub/Sub | — | PDO | Event | ✓ | — | PUB/SUB | ✓ | ✓ |
| Req/Rep | — | SDO | Method | — | Unary | REQ/REP | — | — |
| Pipeline | — | — | — | — | — | PUSH/PULL | — | — |
| Async RPC | CoE | — | — | — | Bidi Stream | ROUTER/DEALER | — | — |
| Field (Get/Set/Notify) | — | — | ✓ | — | — | — | — | — |
| Exclusive Pair | — | — | — | — | — | PAIR | — | — |
| Streaming | — | — | — | — | Server/Client Stream | — | — | — |
| Fire & Forget | — | EMCY | Event | ✓ | — | — | — | — |
| Discovery | — | NMT | SD | SEDP/SPDP | — | — | — | — |

## 环境要求

- CMake 3.10+
- GCC/G++ (C11/C++17)
- ROS2 Humble (仅 `ros2/` 需要)
- 各子目录 README 中包含详细依赖说明和运行命令

## 项目关联

本仓库为 [eir/lixin](.) workspace 的通信知识基础，其他项目中的通信方案均可在此找到 Demo 参考：

| 项目 | 使用的中间件 | 说明 |
|------|------------|------|
| [GATEWAY-MULTI-AGENT](../GATEWAY-MULTI-AGENT/) | MQTT | Python MQTT 客户端 → Bridge → ROS2 → 机器人 |
| [ASR-LLM-TTS](../ASR-LLM-TTS/) | WebSocket, MQTT | WebSocket 音频推流 + MQTT 状态上报 |
| [RTSP-MTX-SERVER](../RTSP-MTX-SERVER/) | WebSocket, RTSP | WebSocket 信令 + RTSP 媒体流 + 飞书 Webhook |

## 许可证

MIT License
