# ROS2 学习

ROS2 (Robot Operating System 2) 是机器人领域的标准中间件，底层基于 DDS 实现，提供发布/订阅、服务调用、Action 等通信模式。

---

## ROS1 → ROS2 → 定制中间件 演进对比

### 为什么会有 ROS2 ?

| 维度 | ROS1 | ROS2 |
|------|------|------|
| **通信层** | 自研 TCPROS (基于 TCP) | 标准 DDS (基于 UDP 组播) |
| **拓扑结构** | 中心化 Master (单点故障) | 去中心化 P2P (无 Master) |
| **发现机制** | ROS Master 集中注册 | DDS SPDP/SEDP 自动发现 |
| **QoS** | 不支持 (尽最大努力) | 完善 QoS 策略 (Reliable/BestEffort/TransientLocal 等) |
| **实时性** | 毫秒级, 无确定性 | 微秒级, 可配置 Deadline/Liveliness |
| **安全性** | 无加密/认证 | DDS Security (TLS + ACL) |
| **跨平台** | 仅 Linux | Linux / Windows / macOS / QNX / VxWorks |
| **消息序列化** | 自定义格式 | CDR/XCDR 标准编码 |
| **多机通信** | 需手动配置 ROS_MASTER_URI | 同一 DDS Domain 自动发现 |
| **定位** | 学术/原型开发 | 产品级 / 可量产 |

**核心结论**: ROS1 是科研性质的工具（Master 挂掉整个系统瘫痪、无法选择可靠性级别、不支持真时实系统），ROS2 通过引入 DDS 解决了这些问题，使得 ROS 生态可以支撑量产项目。

---

### 大厂为什么仍自研中间件？

尽管 ROS2 已经成熟，但在汽车/工业控制领域，一线大厂仍投入大量资源自研中间件。原因如下：

| 需求 | ROS2 的限制 | 自研中间件的做法 |
|------|------------|----------------|
| **硬实时 (<1ms 确定性)** | DDS 依赖 UDP 和系统调度, 难以保证 μs 级确定性 | 共享内存 + 时间触发调度 + 中断级别精确控制 |
| **MCU 资源 (RAM <2MB)** | ROS2/DDS 栈需要 100MB+ RAM, 只能在 MPU 上运行 | 裁剪协议栈, 静态内存分配, 可在 Cortex-M 上运行 |
| **ISO 26262 ASIL 认证** | DDS 实现未经过功能安全认证 | 全链路按照 ASIL-B/D 开发, 每条代码可追溯 |
| **AUTOSAR 兼容** | ROS2 不是 AUTOSAR 标准组件 | 实现 AUTOSAR AP ara::com 接口, 与车辆其他 ECU 互通 |
| **数据主权** | 核心通信能力依赖第三方开源项目 | 完全自主可控, 不受开源维护者变更影响 |
| **确定性调度** | 无法精确控制消息的发送时刻和顺序 | 时间感知调度 (Time-Aware Scheduling), TSN 集成 |

**典型自研案例**:
- **Apex.OS / Apex.Middleware**: 基于 ROS2 接口但重写底层, 通过 ISO 26262 ASIL-D 认证
- **Fusa**: 功能安全相关的中间件层, 独立于 ROS2
- **车载 SOA (Some/IP + DDS)**: 自适应 AUTOSAR 平台上同时运行 Some/IP 和 DDS, 针对不同域使用不同协议

---

## ROS2 核心概念

| 概念 | 说明 |
|------|------|
| Node | 最小执行单元, 一个进程可包含多个 Node |
| Topic | 发布/订阅的主题, 有固定的消息类型 |
| Publisher | 发布者, 向 Topic 发送消息 |
| Subscriber | 订阅者, 从 Topic 接收消息 |
| Service | 同步的请求/响应 RPC |
| Action | 带反馈的长时间任务 (可取消) |
| QoS | 服务质量配置 (可靠性、持久性、历史深度等) |

---

## 项目结构

```
ros2/
├── README.md
├── topic_lab/                       # ★ 1. 基础Pub/Sub + 自定义消息
│   ├── msg/VehicleState.msg           ROS2 .msg ← 对应 DDS VehicleState.idl
│   └── src/
│       ├── basic_pub.cpp              String 发布者
│       ├── basic_sub.cpp              String 订阅者
│       ├── vehicle_pub.cpp            自定义消息发布 (500ms)
│       └── vehicle_sub.cpp            回调接收 + 延迟计算
│
├── service_lab/                     # ★ 2. Service 请求/响应
│   ├── srv/BatteryQuery.srv
│   └── src/
│       ├── battery_server.cpp         维护电池状态, 响应查询
│       └── battery_client.cpp         每 3s 查询, 异步回调
│
├── action_lab/                      # ★ 3. Action 异步任务
│   ├── action/NavigateTo.action
│   └── src/
│       ├── navigate_server.cpp        模拟导航 5s, 200ms 反馈进度
│       └── navigate_client.cpp        发送目标 → 收反馈 → 收结果
│
├── qos_lab/                         # ★ 4. QoS 对比实验
│   └── src/
│       ├── qos_publisher.cpp          Reliable/BestEffort/TransientLocal
│       └── qos_subscriber.cpp         丢帧检测 + 延迟对比
│
├── lifecycle_lab/                   # ★ 5. Lifecycle 生命周期
│   └── src/lifecycle_demo.cpp         Unconfigured→Inactive→Active
│
├── composition_lab/                 # ★ 6. 进程内 Composition
│   └── src/composition_demo.cpp       MultiThreadedExecutor
│
├── param_lab/                       # ★ 7. 动态参数
│   └── src/param_demo.cpp             运行时参数变更
│
├── launch_lab/                      # ★ 8. Launch 文件编排
│   ├── launch/demo_launch.py           Talker+Listener + 参数 + Remap
│   └── src/
│       ├── talker.cpp
│       └── listener.cpp
│
├── tf2_lab/                         # ★ 9. TF2 坐标变换
│   ├── launch/tf2_demo.launch.py       静态+动态变换+监听器
│   └── src/
│       ├── static_broadcaster.cpp      map→odom (静态)
│       ├── robot_broadcaster.cpp       odom→base_link (动态+传感器)
│       └── transform_listener.cpp      全变换链查询
│
├── rosbag2_lab/                     # ★ 10. Rosbag2 录制/回放
│   └── src/
│       ├── data_generator.cpp          50Hz 正弦波测试数据
│       ├── bag_recorder.cpp            C++ API 编程式录制
│       └── bag_player.cpp              C++ API 编程式回放
│
├── loan_lab/                        # ★ 11. Loan Message 零拷贝
│   └── src/
│       ├── loan_publisher.cpp          borrow_loaned_message
│       ├── loan_subscriber.cpp         订阅端吞吐统计
│       └── loan_comparison.cpp         A/B 对比: regular vs loan
│
├── multimachine_lab/                # ★ 12. 多机分布式通信
│   ├── src/
│   │   ├── discovery_publisher.cpp     ROS_DOMAIN_ID 可配置
│   │   └── discovery_subscriber.cpp    自动发现对端
│   └── scripts/
│       ├── setup_multimachine.sh       配置指南
│       └── multimachine_guide.sh       诊断工具
│
├── dds_vendor_lab/                  # ★ 13. DDS 供应商切换
│   ├── src/vendor_test_node.cpp        RMW 无关通用节点
│   ├── scripts/vendor_compare.sh       FastDDS vs CycloneDDS
│   └── README.md                       RMW 抽象层说明
│
├── mqtt_lab/                        # ★ 14. MQTT 桥接
│   ├── src/ros2_mqtt_bridge.cpp        双向 ROS2↔MQTT
│   └── scripts/setup_mosquitto.sh
│
├── dds_bridge/                      # DDS ↔ ROS2 双工桥接
├── scripts/run_all_labs.sh          # 一键启动菜单
├── build/ install/ log/             # colcon 输出
```

---

## DDS ↔ ROS2 桥接

ROS2 底层默认使用 CycloneDDS，意味着原生 DDS 程序和 ROS2 节点可以直接通信：

```
原生 DDS Publisher              DDS Domain 0
(dds/publisher.c)   ───→  Topic: "VehicleState"
                                    │
                             ┌──────┴──────┐
                             │ dds_ros2_bridge │
                             │  ↑ CycloneDDS C API
                             │  ↓ rclcpp        │
                             └──────┬──────┘
                                    │
                             ROS2 Topic: "/vehicle/state"
                                    │
                             ros2 topic echo /vehicle/state
```

### 验证方式

```bash
# 1. 编译 DDS (生成 VehicleState.h)
cd dds && mkdir build && cd build
cmake .. && make

# 2. 编译 ROS2
cd ros2
colcon build --packages-select dds_bridge
source install/setup.bash

# 3. 终端1: 原生 DDS 发布者
./dds/build/publisher

# 4. 终端2: 桥接节点
ros2 run dds_bridge dds_ros2_bridge

# 5. 终端3: 看到 DDS 数据出现在 ROS2!
ros2 topic echo /vehicle/state
```

**重点**: `dds_ros2_bridge` 在同一个进程里同时调用 CycloneDDS C API 和 rclcpp，证明了 ROS2 就是 DDS。

---

## 运行环境

```bash
# Ubuntu 22.04 安装 ROS2 Humble
source /opt/ros/humble/setup.bash
```

## 构建 & 运行 (topic_lab)

```bash
cd ros2
colcon build --packages-select topic_lab
source install/setup.bash

# 终端1: 发布者
ros2 run topic_lab basic_pub

# 终端2: 订阅者
ros2 run topic_lab basic_sub
```

## 关键 API (rclcpp)

```cpp
class Talker : public rclcpp::Node {
    Talker() : Node("talker") {
        publisher_ = this->create_publisher<MsgType>("/topic", 10);
        timer_ = this->create_wall_timer(500ms, std::bind(&Talker::cb, this));
    }
};
```

## ROS2 命令行速查

```bash
ros2 node list            # 列出所有节点
ros2 topic list           # 列出所有主题
ros2 topic echo /topic    # 查看主题数据
ros2 topic hz /topic      # 查看发布频率
ros2 topic info /topic    # 查看主题信息
ros2 service list         # 列出所有服务
```

## 构建 & 运行

### topic_lab (自定义消息)
```bash
ros2 run topic_lab vehicle_publisher
ros2 run topic_lab vehicle_subscriber        # 另一个终端
```

### service_lab
```bash
ros2 run service_lab battery_server
ros2 run service_lab battery_client           # 另一个终端
ros2 service call /battery/query service_lab/srv/BatteryQuery  # CLI 查询
```

### action_lab
```bash
ros2 run action_lab navigate_server
ros2 run action_lab navigate_client           # 另一个终端
ros2 action send_goal /navigate action_lab/action/NavigateTo "{target_x: 20, target_y: 15}"
```

### qos_lab
```bash
# 实验 A: Reliable 不丢帧
ros2 run qos_lab qos_subscriber --ros-args -p qos_mode:=reliable
ros2 run qos_lab qos_publisher --ros-args -p qos_mode:=reliable

# 实验 B: BestEffort 可能丢帧
ros2 run qos_lab qos_subscriber --ros-args -p qos_mode:=besteffort
ros2 run qos_lab qos_publisher --ros-args -p qos_mode:=besteffort

# 实验 C: TransientLocal (先 pub 再 sub 也能拿到历史)
ros2 run qos_lab qos_publisher --ros-args -p qos_mode:=transient  # 先发
ros2 run qos_lab qos_subscriber --ros-args -p qos_mode:=transient  # 后接
```

### lifecycle_lab
```bash
ros2 run lifecycle_lab lifecycle_demo
# 观察 Unconfigured→Inactive→Active→Deactivate→Cleanup 状态转换
```

### composition_lab
```bash
timeout 8 ros2 run composition_lab composition_demo
# PubNode + SubNode 在同一进程, 使用 MultiThreadedExecutor
```

### param_lab
```bash
timeout 10 ros2 run param_lab param_demo
# 演示运行时 add_on_set_parameters_callback 动态调参
```

### launch_lab
```bash
ros2 launch launch_lab demo_launch.py
ros2 launch launch_lab demo_launch.py talker_rate:=5.0  # 覆盖参数
```

### tf2_lab
```bash
ros2 launch tf2_lab tf2_demo.launch.py          # 一键启动 3 节点
ros2 run tf2_tools tf2_echo map laser_frame     # 验证变换链
ros2 run tf2_tools view_frames                  # 生成 PDF 变换树
```

### rosbag2_lab
```bash
ros2 run rosbag2_lab data_generator   # Terminal 1: 生成 50Hz 正弦波
ros2 run rosbag2_lab bag_recorder     # Terminal 2: C++ API 录制
ros2 run rosbag2_lab bag_player       # Terminal 3: C++ API 回放
# CLI 等价操作:
ros2 bag record -o my_bag /sensor/data
ros2 bag play my_bag
```

### loan_lab
```bash
ros2 run loan_lab loan_subscriber    # Terminal 1: 订阅端
ros2 run loan_lab loan_publisher     # Terminal 2: Loan 发布 (100Hz)
ros2 run loan_lab loan_comparison    # A/B 对比: regular vs loan
```

### multimachine_lab
```bash
# Machine A (同子网):
ROS_DOMAIN_ID=5 ros2 run multimachine_lab discovery_publisher --ros-args -p domain_id:=5
# Machine B (同子网):
ROS_DOMAIN_ID=5 ros2 run multimachine_lab discovery_subscriber --ros-args -p domain_id:=5
# Machine B 自动发现 Machine A — 无需配置 IP!
```

### dds_vendor_lab
```bash
# 默认 FastDDS:
ros2 run dds_vendor_lab vendor_test_node --ros-args -p mode:=pub
# 切换到 CycloneDDS (需先 sudo apt install ros-humble-rmw-cyclonedds-cpp):
RMW_IMPLEMENTATION=rmw_cyclonedds_cpp ros2 run dds_vendor_lab vendor_test_node --ros-args -p mode:=sub
# 两者可以通信! — DDS RTPS wire protocol 保证互操作性
```

### mqtt_lab
```bash
bash mqtt_lab/scripts/setup_mosquitto.sh       # 首次: 安装 mosquitto
ros2 run mqtt_lab ros2_mqtt_bridge              # 启动桥接
mosquitto_sub -t "ros2/data"                    # 查看 ROS2→MQTT 数据
ros2 topic echo /mqtt_cmd                       # 查看 MQTT→ROS2 数据
```

### 换到非 DDS 传输: zenoh_lab

`dds_vendor_lab` 只在 DDS 家族内部换供应商 (FastDDS ⇄ CycloneDDS),两者靠 RTPS 线协议互通。
RMW 抽象层还能换到**根本不是 DDS** 的传输 —— 见仓库根目录的 [`../zenoh/`](../zenoh/):

```bash
# 换 RMW: 整条传输栈变成 Zenoh
ros2 run rmw_zenoh_cpp rmw_zenohd                # 先起 zenoh 路由器
RMW_IMPLEMENTATION=rmw_zenoh_cpp ros2 run ros2_zenoh_lab rmw_probe_node --ros-args -p mode:=pub

# 或挂桥接: DDS 侧零改动, 把 ROS2 图桥进 zenoh 网络
bash ../zenoh/scripts/run_demo.sh bridge
```

注意: 这两种方式**互不通**,隔着两层墙 —— Humble 上 `rmw_zenoh_cpp` 内置 zenoh-c 1.8.0,
而 `zenoh_bridge_dds` 还是 zenoh 0.5.x 那一代,桥接器连它的路由器会直接核心转储;
即使能握手,两者的键表达式命名空间也没有交集。
详见 [`../zenoh/README.md`](../zenoh/README.md)。

## 进阶学习方向

- [x] DDS ↔ ROS2 桥接 (同进程 CycloneDDS + rclcpp) — `dds_bridge`
- [x] 自定义消息类型 (.msg) — `topic_lab`
- [x] Service 请求/响应 (.srv) — `service_lab`
- [x] Action 异步任务+反馈 (.action) — `action_lab`
- [x] QoS 策略对比 (Reliable/BestEffort/TransientLocal) — `qos_lab`
- [x] Lifecycle 生命周期 — `lifecycle_lab`
- [x] 进程内 Composition — `composition_lab`
- [x] 动态参数 — `param_lab`
- [x] Launch 文件编排 — `launch_lab`
- [x] TF2 坐标变换 — `tf2_lab`
- [x] Rosbag2 录制/回放 — `rosbag2_lab`
- [x] 零拷贝 Loan Message — `loan_lab`
- [x] 多机分布式通信 — `multimachine_lab`
- [x] DDS 供应商切换 — `dds_vendor_lab`
- [x] ROS2 + MQTT Bridge 集成 — `mqtt_lab`
