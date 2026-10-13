# MQTT 学习

MQTT (Message Queuing Telemetry Transport) 是一种轻量级的发布/订阅消息传输协议，广泛应用于 IoT、车联网等场景。

> 本项目模拟车载 CAN 数据采集 → 信号解析 → Protobuf/JSON 序列化 → MQTT 上云的完整链路，并用三个发布者分别演示 **MQTT v3.1.1**、**MQTT v5** 与 **GB/T 32960 国标上报**。

## MQTT 核心概念

- **Broker**: 消息代理服务器，负责接收和转发消息
- **Topic**: 消息主题，发布者向主题发送消息，订阅者订阅主题接收消息
- **QoS**: 服务质量等级 (0: 最多一次, 1: 至少一次, 2: 恰好一次)
- **Retained Message**: 保留消息，新订阅者会收到最后一条保留消息
- **Last Will**: 遗嘱消息，客户端异常断开时自动发送
- **Topic Alias** (v5): 长 Topic 名压缩为整型别名，节省每次发布的带宽
- **Session Expiry** (v5): 断连后 Broker 保留会话的时长，重连后自动恢复订阅

---

## 项目结构

```
mqtt/
├── CMakeLists.txt                    # 顶层构建脚本 (C++17)
├── README.md
│
├── config/
│   └── can_mqtt.conf                 # ★ 运行时配置 (broker / topic / QoS / 采集参数)
│
├── cmake/
│   └── FindPahoMqttC.cmake           # Find 模块 (third-lib 优先)
│
├── include/mqtt_can/                 # 公共头文件 (C++ 接口)
│   ├── can_database.hpp              # CAN 信号数据库 (DBC 风格: 5 帧 12 信号)
│   ├── can_simulator.hpp             # CAN 总线模拟器 (冷启动 / 行驶状态)
│   ├── can_hardware.hpp              # SocketCAN 真实硬件接入 (USE_SOCKETCAN 编译开关)
│   ├── can_parser.hpp                # 按位解析 → 物理量 + 信号快照
│   ├── config_manager.hpp            # key=value 配置解析 (零依赖)
│   ├── mqtt_client.hpp               # Paho MQTTAsync RAII 封装 (v3.1.1 / v5 / 重连)
│   └── report_scheduler.hpp          # 国标 / 企标上报调度器 (周期 + 变化)
│
├── src/
│   ├── lib/                          # ↑ 各头文件对应实现 → libmqtt_can.a
│   │   ├── can_database.cpp
│   │   ├── can_simulator.cpp
│   │   ├── can_hardware.cpp
│   │   ├── can_parser.cpp
│   │   ├── config_manager.cpp
│   │   ├── mqtt_client.cpp
│   │   └── report_scheduler.cpp
│   ├── mqtt_can_publisher.cpp        # 发布者 v3.1.1: CAN → Protobuf + JSON
│   ├── mqtt_can_publisher_v5.cpp     # 发布者 v5: Topic Alias + Session Expiry + 自动重连
│   ├── mqtt_can_publisher_gb32960.cpp# 国标上报: 周期 + 变化双策略
│   └── mqtt_can_subscriber.cpp       # 订阅者: Protobuf / JSON 解码展示
│
├── demo/                             # 最小 Demo (C)
│   ├── publisher.c
│   └── subscriber.c
│
├── proto/
│   └── vehicle_can.proto             # CanFrame / VehicleSignals (protobuf-c 生成)
│
├── scripts/
│   ├── build_deps.sh                 # 交叉编译第三方库 → third-lib/
│   └── generate_proto.sh             # 手动生成 protobuf-c 代码
│
└── third-lib/                        # 预置第三方库产物 (无需板子安装)
    ├── paho-mqtt/
    │   ├── include/                  #   MQTTAsync.h ...
    │   └── lib/                      #   libpaho-mqtt3as.a
    └── protobuf-c/
        ├── include/                  #   protobuf-c.h ...
        └── lib/                      #   libprotobuf-c.a
```

---

## 数据流架构

```
┌──────────────┐    原始CAN帧     ┌──────────────┐
│ CanSimulator │ ───────────────→ │  CanParser   │
│ (CAN总线模拟) │  .can_id, .data │ (信号解析器)  │
│ 或 SocketCAN │                  └──────┬───────┘
│  (can0/vcan0)│                         │
└──────────────┘              物理信号 float
                                         ▼
                        ┌────────────────┴────────────────┐
                        │                                 │
                 ┌──────┴───────┐                 ┌───────┴────────┐
                 │  Protobuf    │                 │ ReportScheduler│
                 │ (protobuf-c) │                 │ 周期 + 变化策略 │
                 └──────┬───────┘                 └───────┬────────┘
                        │ 二进制 bytes                     │ JSON
                        ▼                                 ▼
                 ┌────────────────────────────────────────────────┐
                 │                  MQTT Broker                   │
                 │            (test.mosquitto.org / 自建)          │
                 └───────┬───────────┬───────────┬────────────────┘
                         │           │           │
        vehicle/can/raw  │  …/signals│  …/json   │  vehicle/gb32960/*
                         ▼           ▼           ▼
                    ┌──────────────────────────────────┐
                    │  mqtt_can_subscriber (Protobuf/  │
                    │  JSON 解码 + 信号展示)            │
                    └──────────────────────────────────┘
```

---

## 三个发布者

| 可执行文件 | 协议 | 演示重点 |
|-----------|------|---------|
| `mqtt_can_publisher` | MQTT v3.1.1 | 基础链路: 原始帧 (QoS 0) + Protobuf 信号快照 + JSON 调试通道 |
| `mqtt_can_publisher_v5` | **MQTT v5** | **Topic Alias** (把长 Topic 压成整型别名)、**Session Expiry=3600s**、指数退避自动重连 (500ms → 30s) |
| `mqtt_can_publisher_gb32960` | MQTT v5 | **GB/T 32960-2016 国标上报**: `ReportScheduler` 周期 (≤30s) + 变化 (阈值触发) 双策略，整车状态 `running/charging/parked` 随载荷上报 |

三者共用 `libmqtt_can.a`，差异只在协议配置与上报策略：

```cpp
// v5 的关键三行
mqtt_cfg.mqtt_version       = 5;      // 切到 v5
mqtt_cfg.session_expiry_sec = 3600;   // 断连 1 小时内 Broker 保留会话
mqtt_cfg.auto_reconnect     = true;   // 指数退避自动重连

int alias = mqtt.register_topic_alias("vehicle/can/signals");  // 之后该 Topic 自动用别名发送
```

国标调度器的信号定义写在 `report_scheduler.cpp` 里：

| 分组 | 策略 | 信号 |
|------|------|------|
| 国标整车 | 周期 30s | `VehicleSpeed` / `Odometer` / `GearPosition` |
| 国标电机 | 周期 30s | `EngineSpeed` |
| 国标热管理 | 周期 30s (阈值 5) | `CoolantTemp` / `FuelLevel` |
| 国标极值 | 周期 30s | `BatteryVoltage` / `ThrottlePosition` |
| 国标报警 | 变化即报 | `BrakeSwitch` |
| 企标扩展 | 变化 (带阈值) | `SteeringAngle` / `SteeringRate` / `ThrottlePosition` / `BatteryVoltage` |

---

## 构建 & 运行

### 本地编译 (x86_64 / ARM 板载)

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)

# 终端1: 发布 CAN 数据 (选一个版本)
./mqtt_can_publisher              # v3.1.1
./mqtt_can_publisher_v5           # v5
./mqtt_can_publisher_gb32960      # 国标上报

# 终端2: 订阅并解码展示
./mqtt_can_subscriber
```

三个可执行文件都支持 `--config path/to.conf` 覆盖默认配置路径。

> 默认使用 `test.mosquitto.org` 公共测试 broker，无需本地部署。

### 方式一：使用 third-lib (推荐 — 无需在板子上装依赖)

> 在 x86_64 构建机上交叉编译，产物放入 `third-lib/`，板子零依赖运行。

```bash
# 1. 安装交叉工具链并编译第三方库
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
bash scripts/build_deps.sh aarch64     # 也支持 armv7l / x86_64

# 2. 交叉编译本项目
mkdir build && cd build
cmake .. \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++
make -j$(nproc)

# 3. 拷贝到板子 (只复制可执行文件，不需要任何 .so)
scp mqtt_can_publisher* mqtt_can_subscriber user@board:/home/user/
```

在板子上直接 `./mqtt_can_publisher`，无需 `apt install` 任何东西。

### 方式二：板子上直接安装依赖

```bash
sudo apt install libpaho-mqtt-dev protobuf-c-compiler libprotobuf-c-dev
mkdir build && cd build && cmake .. && make -j$(nproc)
```

### 简单 Demo (原始版本, JSON + C 接口)

```bash
mkdir build && cd build && cmake .. && make
./publisher      # 终端1
./subscriber     # 终端2
```

### CMake 选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `BUILD_CAN_DEMO` | ON | 构建 libmqtt_can + 4 个 CAN 程序 (v3 / v5 / gb32960 / subscriber) |
| `BUILD_SIMPLE_DEMO` | ON | 构建简单 JSON Demo (`publisher` / `subscriber`) |

```bash
cmake .. -DBUILD_CAN_DEMO=OFF
```

---

## 配置文件

`config/can_mqtt.conf` 用零依赖的 `key=value` 格式，由 `ConfigManager` 解析 (支持 `#` 注释、点号分层、类型安全取值与默认值回退)。CMake 会自动把它拷到 `build/` 目录，所以从 `build/` 里运行即生效。

```ini
broker.address  = tcp://test.mosquitto.org:1883
broker.keepalive = 60

publisher.client_id  = vehicle-can-publisher-001
publisher.vehicle_id = VIN_LX_TEST_001
publisher.cold_start = false                 # true = 冷启动状态 (车速 0、冷却液低温)
publisher.publish_interval_ms = 1000

# Topic & QoS (发布/订阅两侧必须一致)
topics.raw.topic     = vehicle/can/raw
topics.raw.qos       = 0                     # 原始帧量大 → QoS 0
topics.signals.topic = vehicle/can/signals
topics.signals.qos   = 1                     # 信号快照重要 → QoS 1
topics.json.topic    = vehicle/can/json
topics.json.qos      = 1
```

### 接入真实 CAN 硬件

`can_hardware.hpp` 提供统一的 `CanDataSource` 接口，编译时开关决定用哪一侧：

| 编译选项 | 数据来源 |
|---------|---------|
| 未定义 `USE_SOCKETCAN` (默认) | `CanSimulator` 生成模拟数据 |
| `-DUSE_SOCKETCAN` | `SocketCanReader` 读取真实 `can0` / `vcan0` (Linux SocketCAN) |

```bash
# 虚拟 CAN 验证
sudo modprobe vcan && sudo ip link add dev vcan0 type vcan && sudo ip link set up vcan0
```

---

## 模拟的 CAN 信号

| CAN ID | 报文名 | 周期 | 信号 | 范围 | 单位 |
|--------|--------|------|------|------|------|
| 0x100 | PowerTrain | 10ms | EngineSpeed | 0~16000 | rpm |
| | | | VehicleSpeed | 0~500 | km/h |
| | | | BrakeSwitch | 0/1/2 | — |
| 0x200 | ThermalElectric | 100ms | CoolantTemp | -40~215 | °C |
| | | | BatteryVoltage | 0~65 | V |
| | | | FuelLevel | 0~100 | % |
| 0x300 | DriverControl | 20ms | ThrottlePosition | 0~100 | % |
| | | | GearPosition | P/R/N/D/S | — |
| 0x400 | Steering | 10ms | SteeringAngle | -780~780 | deg |
| | | | SteeringRate | 0~1016 | deg/s |
| 0x500 | Instrument | 100ms | Odometer | 0~1e6 | km |

---

## MQTT Topic 定义

| Topic | 载荷 | QoS | 说明 |
|-------|------|-----|------|
| `vehicle/can/raw` | Protobuf `CanFrame` | 0 | 原始 CAN 帧 (二进制) |
| `vehicle/can/signals` | Protobuf `VehicleSignals` | 1 | 解析后的物理信号快照 (1Hz, v5 版走 Topic Alias) |
| `vehicle/can/json` | JSON | 1 | 解析后的信号 (人类可读, 调试用) |
| `vehicle/gb32960/{signal}` | JSON | 1 | 国标周期 / 变化上报 (如 `vehicle/gb32960/vehicle_speed`) |
| `vehicle/enterprise/{signal}` | JSON | 1 | 企标变化上报 (如 `vehicle/enterprise/steering_angle`) |

上报 JSON 的形态：

```json
{
  "vehicle_id": "VIN_LX_TEST_001",
  "ts": 1791792486,
  "status": "running",
  "signal": "vehicle_speed",
  "value": 62.5,
  "unit": "km/h",
  "reason": "periodic",
  "policy": "periodic"
}
```

---

## 核心模块 API

### CanDatabase — CAN 信号数据库

```cpp
#include "mqtt_can/can_database.hpp"

using namespace mqtt_can;

// 初始化默认数据库 (5帧12信号)
CanDatabase::instance().init_default();
CanDatabase::instance().print();

// 查找
const CanMessage* msg = CanDatabase::instance().find_message(0x100);
const CanSignal* sig = CanDatabase::instance().find_signal("VehicleSpeed");

// 解析
uint64_t raw = extract_bits(data, sig->start_bit, sig->length, ByteOrder::Intel);
float speed = sig->decode(raw);
```

### CanSimulator — CAN 总线模拟器

```cpp
#include "mqtt_can/can_simulator.hpp"

CanSimulator sim(/*cold_start=*/false);   // false=行驶状态, true=冷启动

auto frames = sim.tick(8);                // 按周期获取帧列表

const auto& state = sim.state();
std::cout << state.vehicle_speed << " km/h\n";
```

### CanParser — CAN 帧解析器

```cpp
#include "mqtt_can/can_parser.hpp"

CanParser parser("VIN_LX_TEST_001");

parser.feed(frame);

auto speed = parser.get("VehicleSpeed");
if (speed) {
    std::printf("车速: %.1f km/h\n", *speed);
}

parser.print_snapshot();
```

### MqttClient — MQTT RAII 封装 (v3.1.1 / v5)

```cpp
#include "mqtt_can/mqtt_client.hpp"

MqttClient::Config cfg;
cfg.broker_address   = "tcp://localhost:1883";
cfg.client_id        = "my-client";
cfg.mqtt_version     = 5;                 // 0 = v3.1.1, 5 = v5
cfg.session_expiry_sec = 3600;            // 仅 v5 生效
cfg.auto_reconnect   = true;              // 指数退避: min 500ms → max 30s

MqttClient mqtt(cfg);
mqtt.set_on_status([](const std::string& s, const std::string& d) {
    std::printf("[MQTT] %s (%s)\n", s.c_str(), d.c_str());   // connected/disconnected/reconnecting/lost
});
mqtt.set_on_message([](const std::string& topic, const uint8_t* data, size_t len) {
    std::printf("[%s] %zu bytes\n", topic.c_str(), len);
});

int alias = mqtt.register_topic_alias("vehicle/can/signals");  // v5: 注册后自动用别名发布

mqtt.connect();
mqtt.subscribe({{"vehicle/can/signals", 1}, {"vehicle/can/json", 1}});
mqtt.publish("vehicle/test", "hello", 5, /*qos=*/1);

// ... 保持运行 ...

mqtt.disconnect(); // RAII 自动清理
```

### ConfigManager — 配置解析

```cpp
#include "mqtt_can/config_manager.hpp"

ConfigManager cfg;
if (!cfg.load("config/can_mqtt.conf")) return 1;

std::string broker = cfg.get_string("broker.address", "tcp://test.mosquitto.org:1883");
int         keepalive = cfg.get_int("broker.keepalive", 60);
bool        cold_start = cfg.get_bool("publisher.cold_start", false);
```

### ReportScheduler — 国标 / 企标上报调度

```cpp
#include "mqtt_can/report_scheduler.hpp"

ReportScheduler gb(gb32960_signals(), "VIN_LX_TEST_001");   // 预置国标信号表
ReportScheduler ent(enterprise_signals(), "VIN_LX_TEST_001");

gb.set_vehicle_status("charging");   // charging / running / parked, 随上报载荷携带

// 每帧调用: 由调度器决定本次是否需要上报 (周期到期 or 超过变化阈值)
gb.tick(parser, [&](const std::string& json, const std::string& signal) {
    mqtt.publish("vehicle/gb32960/" + signal, json.data(), json.size(), 1);
});
```

---

## 关键 API (Paho MQTT C)

```c
// 创建客户端 (异步模式)
MQTTAsync_create(&client, address, client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL);

// 设置消息回调
MQTTAsync_setCallbacks(client, NULL, NULL, on_message, NULL);

// 连接
MQTTAsync_connect(client, &conn_opts);

// 发布
MQTTAsync_sendMessage(client, topic, &msg, &pub_opts);

// 订阅
MQTTAsync_subscribeMany(client, count, topics, qos_array, &sub_opts);

// v5: 连接属性 —— 会话过期 + 关闭 cleansession (MqttClient 内部封装)
MQTTProperties props = MQTTProperties_initializer;
MQTTProperty prop;
prop.identifier     = MQTTPROPERTY_CODE_SESSION_EXPIRY_INTERVAL;
prop.value.integer4 = 3600;
MQTTProperties_add(&props, &prop);
opts.MQTTVersion       = 5;
opts.cleansession      = 0;              // v5 下不清会话, 重连后自动恢复订阅
opts.connectProperties = &props;
```

---

## 进阶学习方向

- [x] CAN 数据模拟 + 信号解析 (DBC-like)
- [x] Protobuf 序列化上传 vs JSON
- [x] SocketCAN 真实硬件接入 (`USE_SOCKETCAN`, `can0`/`vcan0`)
- [x] 配置文件化 (broker / topic / QoS / 采集参数)
- [x] MQTT v5 新特性 (Topic Alias, Session Expiry)
- [x] 断线指数退避自动重连 + 连接状态回调
- [x] GB/T 32960 国标周期上报 + 企标变化上报
- [x] ARM 交叉编译 (aarch64 / armv7l, third-lib 零依赖部署)
- [ ] QoS 等级对延迟/可靠性的实测对比
- [ ] TLS/SSL 安全通信 (8883 端口 + 双向证书)
- [ ] 大规模 Topic 下的性能测试
- [ ] 对接真实 CAN 硬件 (PCAN / Kvaser / 车规网关)
- [ ] MQTT Bridge 跨 broker 通信
- [ ] 数据落盘 + 回放 (CAN 原始帧存储)
