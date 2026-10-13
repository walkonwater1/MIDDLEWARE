# CANopen — 基于 CAN 总线的应用层中间件

> 机器人关节电机 / 传感器最常用的通信协议栈

## 核心概念

CANopen 是 CAN 之上的应用层协议，提供了类似中间件的抽象：

| 抽象 | 类比 | 说明 |
|------|------|------|
| **PDO** (过程数据对象) | Pub/Sub Event | 生产者-消费者模式，最多 8 字节，实时推送 |
| **SDO** (服务数据对象) | RPC Method | 请求-响应模式，读写对象字典，配置参数 |
| **NMT** (网络管理) | Service Discovery | 状态机控制（启动/停止/复位），心跳检测 |
| **EMCY** (紧急报文) | Error Event | 设备异常时主动上报故障码 |
| **对象字典 (OD)** | 数据模型 | 每个设备的标准化参数表（类似 DDS 的 Topic Schema） |

## NMT 状态机

```
        上电/复位
           ↓
       Initialization
           ↓
       Pre-Operational  ←→  Stopped
           ↓
       Operational
```

| 状态 | PDO | SDO | NMT | 说明 |
|------|-----|-----|-----|------|
| Stopped | ✗ | ✗ | ✓ | 不可通信 |
| Pre-Operational | ✗ | ✓ | ✓ | 只允许配置 |
| Operational | ✓ | ✓ | ✓ | 正常工作 |

## COB-ID 分配（CAN-ID 结构）

```
11-bit CAN-ID = Function Code (4 bit) + Node-ID (7 bit)

PDO1(tx): 0x180 + Node-ID
PDO1(rx): 0x200 + Node-ID
PDO2(tx): 0x280 + Node-ID
PDO2(rx): 0x300 + Node-ID
SDO(tx):  0x580 + Node-ID
SDO(rx):  0x600 + Node-ID
NMT:      0x000        (broadcast)
Heartbeat:0x700 + Node-ID
EMCY:     0x080 + Node-ID
```

## 项目结构

```
canopen/
├── canopen.hpp               # 协议定义: COB-ID / NMT 状态机 / SDO CS·Abort 码 / OD / PDO / EMCY
├── canopen.cpp               # 核心实现: ObjectDictionary / NmtStateMachine / VirtualCanBus
├── canopen_node.cpp          # 从站: PDO 生产 + SDO 服务端 + Heartbeat + EMCY
├── canopen_master.cpp        # 主站: NMT 命令 + SDO 客户端 + PDO 消费 + 心跳/EMCY 监控
├── include/
│   └── vcan_bridge.hpp       # SocketCAN 桥接: VirtualCanBus ⇄ 真实 vcan0/can0 双向转发
├── scripts/
│   └── run_demo.sh           # 一键脚本 (both / master / node / vcan / build / clean)
└── CMakeLists.txt
```

## SDO 协议实现细节

`canopen_node.cpp` 实现的是完整的 SDO 服务端 (expedited transfer)，不是只回固定应答：

| 方向 | Command Specifier | 说明 |
|------|------------------|------|
| 客户端 → 服务端 | `0x40` | Initiate Upload (读) |
| 客户端 → 服务端 | `0x23 / 0x2B / 0x22 / 0x21` | Initiate Download (1 / 2 / 3 / 4 字节) |
| 服务端 → 客户端 | `0x4B` | Upload Response (4 字节 expedited) |
| 服务端 → 客户端 | `0x60` | Download Response |
| 服务端 → 客户端 | `0x80` | Abort SDO Transfer (携带 4 字节 Abort Code) |

Abort Code 按 CiA 301 定义 (`SdoAbortCode`)：`0x06010000` 不支持、`0x06010002` 只读、`0x06020000` 对象不存在、`0x06090030` 数值超范围。写入前先过 `ObjectDictionary::validate_range()`，不合法直接回 Abort。从站对象字典里包含 CiA 402 的目标位置对象 `0x607A Target Position`。

## 本 Demo 架构

```
┌─────────────────────────────────────────┐
│            canopen_master               │
│  - NMT 命令 (启停、复位)                 │
│  - SDO 读写 (配置电机参数)               │
│  - PDO 接收 (位置/速度/电流)             │
│  - 心跳监控                             │
└──────────┬──────────────────────────────┘
           │  (virtual CAN / vcan0)
┌──────────┴──────────────────────────────┐
│        canopen_node (Slave #1)          │
│  - 对象字典 (位置/速度/转矩/电流)         │
│  - TPDO1: 位置+速度 (1ms 周期)          │
│  - TPDO2: 转矩+电流 (10ms 周期)         │
│  - RPDO1: 目标位置+控制字               │
│  - Heartbeat: 100ms                     │
│  - EMCY: 过流/过温告警                  │
└─────────────────────────────────────────┘
```

## 编译运行

```bash
# 方式一: 一键脚本 (自动编译 + 从站后台 + 主站前台)
cd canopen && bash scripts/run_demo.sh both     # 纯用户态虚拟总线, 不需要 root

bash scripts/run_demo.sh vcan                   # 创建 vcan0 并开启 SocketCAN 桥接 (需 sudo)
# 其他子命令: master / node / build / clean
```

```bash
# 方式二: 手动分步
sudo apt install can-utils         # vcan 虚拟 CAN 接口 (可选)

sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

cd canopen && mkdir build && cd build && cmake .. && make

./canopen_node     # 终端1: 启动从站
./canopen_master   # 终端2: 启动主站
```

## 从站对象字典 (节选)

`ObjectDictionary` 里是按 CiA 301 / **CiA 402 驱动子协议**组织的对象 (单位: 索引 0x6000~0x6FFF):

| 索引 | 对象 | 访问 | 说明 |
|------|------|------|------|
| 0x1000 | Device Type | RO | `0x00020192` |
| 0x1017 | Producer Heartbeat Time | RW | 默认 100ms, 主站会用 SDO 改成 200ms |
| 0x1018 | Vendor / Product / Revision / Serial | RO | 设备标识 |
| 0x1400 + 0x1600 | RPDO1 通信参数 + 映射 | RW | `0x6040` Controlword + `0x607A` Target Position |
| 0x1800 + 0x1A00 | TPDO1 通信参数 + 映射 | RW | `0x6064` Actual Position + `0x606C` Actual Velocity |
| 0x1801 + 0x1A01 | TPDO2 通信参数 + 映射 | RW | `0x6077` Actual Torque + `0x6078` Actual Current |
| 0x6040 / 0x6041 | Controlword / Statusword | RW / RO | CiA 402 驱动状态机 |
| 0x607A | Target Position | RW | 主站每 100ms 通过 RPDO1 下发正弦目标位置 |

## 运行输出示例

`bash scripts/run_demo.sh both` (数值随运行变化)：

```
=== CANopen Node #1 Starting ===
=== CANopen Master Starting ===
[MASTER] Waiting for slave bootup...
[MASTER] → NMT Enter Pre-Operational
[NODE#1] NMT command: 0x80
[MASTER] SDO read Device Type (0x1000)
[MASTER] SDO ← [0x1000:00] = 0x00020192 (131474)
[MASTER] SDO read Vendor ID (0x1018:01)
[MASTER] SDO ← [0x1018:01] = 0x0000ABCD (43981)
[MASTER] → NMT Start Remote Node
[MASTER] Heartbeat node#1: OPERATIONAL
[MASTER] Operational: sending target position every 100ms...
[NODE#1] TPDO1 → pos=1024   vel=300
[MASTER] PDO1 ← node#1 pos=1024 vel=300 (x100)
[MASTER] SDO write Heartbeat time → 200ms
[NODE#1] SDO download [0x1017:00] OK
[NODE#1] RPDO1 ← ctrl=0x000F target=2048
[NODE#1] EMCY → TEMPERATURE: 85°C
[MASTER] EMCY ← node#1 code=0x4000 reg=0x00
[MASTER] → NMT Stop Remote Node
=== CANopen Master Stopped ===
```

## 关键文件

| 文件 | 说明 |
|------|------|
| `canopen.hpp` | COB-ID / NMT 状态机 / SDO CS + Abort 码 / 对象字典 / PDO / EMCY 定义 |
| `canopen.cpp` | `ObjectDictionary` (读写 + 范围校验) / `NmtStateMachine` / `VirtualCanBus` |
| `canopen_node.cpp` | 从站实现：PDO 生产、SDO 服务端 (Upload/Download/Abort)、Heartbeat、EMCY |
| `canopen_master.cpp` | 主站实现：NMT 命令、SDO 客户端、PDO 消费、心跳与 EMCY 监控 |
| `include/vcan_bridge.hpp` | 虚拟总线 ⇄ 真实 `vcan0`/`can0` 双向桥接 |
| `scripts/run_demo.sh` | 一键编译运行 (含 vcan 模式) |

## CANopen 在机器人中的应用

| 组件 | COB-ID 用途 | PDO 内容 |
|------|-----------|----------|
| 关节电机 | PDO1/2 | 位置、速度、转矩、电流 |
| IMU | PDO1 | 加速度 xyz、角速度 xyz |
| 编码器 | PDO1 | 多圈位置、转速 |
| 力传感器 | PDO1 | 6 维力/力矩 |
| 急停开关 | EMCY + PDO | 安全回路状态 |

## 进阶学习方向

- [x] CiA 301 应用层与通讯框架 (NMT / SDO / PDO / EMCY / Heartbeat)
- [x] CiA 402 驱动器对象字典 (Controlword / Statusword / Target Position / Actual Velocity ...)
- [x] SocketCAN `vcan0` 双向桥接 (`include/vcan_bridge.hpp`)
- [ ] SDO 分段传输 (Segmented Transfer) —— 当前只实现了 expedited (≤4 字节)
- [ ] PDO 映射优化：利用 8 字节极限打包
- [ ] SYNC 同步帧的 PDO 触发机制
- [ ] 多轴同步：利用 SYNC + PDO 实现分布式时钟
- [ ] CAN 转 ROS2 (ros2_canopen) 桥接实践
- [ ] CAN FD 适配：突破 8 字节限制，支持大数据 PDO
- [ ] 与 SOME/IP 的对比：车载 CAN ↔ 以太网网关设计

## 参考资源

- CiA 301 v4.2.0 — CANopen Application Layer and Communication Profile
- CiA 402 — Device Profile for Drives and Motion Control
- [CANopenNode](https://github.com/CANopenNode/CANopenNode) — 开源 CANopen 协议栈
