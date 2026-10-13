# DDS 学习

DDS (Data Distribution Service) 是 OMG 组织定义的去中心化实时数据分发标准，广泛用于自动驾驶、军工、航空等高可靠实时系统。**ROS2 的底层通信就是 DDS**。

## DDS 核心概念

| 概念 | 说明 |
|------|------|
| Domain | 通信域, 同一域内的 Participant 才能通信 |
| Participant | 通信参与者, 一个进程通常创建一个 |
| Topic | 数据主题, 绑定一个数据类型 |
| Writer / Reader | 实际发布 / 订阅数据的实体 |
| QoS | 20+ 种服务质量策略组合 |
| Partition | Domain 内的逻辑隔离区 |

## 运行环境

```bash
sudo apt install cyclonedds-dev
```

## 构建

```bash
mkdir build && cd build
cmake .. && make -j$(nproc)
```

## 一键脚本

`scripts/` 下三个脚本覆盖全部实验：

```bash
# 车载实验: 交互菜单, 或直接指定子实验
./scripts/run_vehicle_demo.sh                # 菜单
./scripts/run_vehicle_demo.sh pubsub         # 基础发布/订阅
./scripts/run_vehicle_demo.sh qos            # QoS 对比实验
./scripts/run_vehicle_demo.sh discovery      # 3 进程自动发现
./scripts/run_vehicle_demo.sh listener       # Listener 回调
./scripts/run_vehicle_demo.sh partition      # Partition 隔离
./scripts/run_vehicle_demo.sh --no-build ... # 跳过编译

# 机器人中控: service + monitor (对标 EHR 项目状态机架构)
./scripts/run_robot_demo.sh                  # 自动编译 + 启动
./scripts/run_robot_demo.sh --service        # 只起服务端
./scripts/run_robot_demo.sh --monitor        # 只起监控端

# DDS Security: 生成 CA 根证书 / 节点证书 / governance.xml / permissions.xml
./scripts/gen_security.sh
```

## Demo 清单

| 可执行文件 | 文件 | 学习点 |
|-----------|------|--------|
| `publisher` / `subscriber` | vehicle/publisher.c / vehicle/subscriber.c | 基础 Pub/Sub, Reliable+TransientLocal |
| `qos_lab` | vehicle/qos_lab.c | **QoS 实验**: Reliable vs BestEffort, TransientLocal vs Volatile, Late Joiner |
| `discovery_lab` | vehicle/discovery_lab.c | **自动发现**: 3 个进程零配置通信 |
| `listener_lab` | vehicle/listener_lab.c | **Listener 回调**: Push 模式 (ROS2 spin 的底层) |
| `partition_lab` | vehicle/partition_lab.c | **Partition 隔离**: 同 Domain 内逻辑分区 |
| `content_filter_lab` | vehicle/content_filter_lab.c | **Content Filter**: Topic 级过滤, 只收符合条件的数据 |
| `instance_lab` | vehicle/instance_lab.c | **Instance 管理**: @key + dispose/unregister 生命周期 |
| `security_lab` | vehicle/security_lab.c | **DDS Security**: 认证/授权/加密三层安全机制 |
| `bench_lab` | vehicle/bench_lab.c | **性能 Benchmark**: 延迟 P50/P99 + 吞吐量 msg/s |
| `robot_dds_service` | robot/robot_dds_service.c | **机器人服务端**: Listener 回调, Task→Status 状态机 |
| `robot_dds_monitor` | robot/robot_dds_monitor.c | **机器人监控端**: 三 Topic Listener, dds_take 实时延迟 |

---

## 实验指南

### 1. 基础 Pub/Sub

```bash
./subscriber &    # 终端1
./publisher       # 终端2
```

### 2. QoS 实验 (最推荐)

```bash
# 实验A: Reliable 不丢帧
./qos_lab sub reliable &
./qos_lab pub reliable        # sub 收到全部 10 条

# 实验B: BestEffort 可能丢帧
./qos_lab sub besteffort &
./qos_lab pub besteffort      # sub 可能丢部分数据

# 实验C: Late Joiner — TransientLocal 保留历史
./qos_lab pub transient &     # 先发 5 条
sleep 6
./qos_lab sub transient       # ★ sub 能收到前 5 条历史!
```

### 3. 自动发现(3 终端)

```bash
./discovery_lab pub1    # 终端1: 发布者1
./discovery_lab pub2    # 终端2: 发布者2  
./discovery_lab sub     # 终端3: 自动收到两份数据 ★ 无需配 IP!
```

### 4. Listener 回调

```bash
./publisher &           # 终端1
./listener_lab          # 终端2: 数据到达自动回调, 不轮询
```

### 5. Partition 隔离

```bash
./partition_lab pub A & ./partition_lab pub B &
./partition_lab sub A   # 只收到 A
./partition_lab sub B   # 只收到 B ← 互相看不到对方数据
```

---

## 关键 API

```c
// 创建 Participant
dds_entity_t pp = dds_create_participant(DOMAIN_ID, NULL, NULL);

// 从 IDL 生成描述符创建 Topic
dds_entity_t tp = dds_create_topic(pp, &vehicle_VehicleState_desc,
                                    "VehicleState", NULL, NULL);

// QoS 配置
dds_qos_t *qos = dds_create_qos();
dds_qset_reliability(qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
dds_qset_durability(qos, DDS_DURABILITY_TRANSIENT_LOCAL);
dds_qset_history(qos, DDS_HISTORY_KEEP_LAST, 10);

// 创建 Writer / Reader
dds_entity_t wr = dds_create_writer(pp, tp, qos, NULL);
dds_entity_t rd = dds_create_reader(pp, tp, qos, NULL);

// Write / Read
dds_write(wr, &sample);
dds_read(rd, samples, infos, max, max);

// Listener 模式 (Push, 非轮询)
dds_listener_t *l = dds_create_listener(NULL);
dds_lset_data_available(l, on_data_available);  // 数据到达回调
dds_lset_subscription_matched(l, on_matched);   // 匹配通知回调
dds_set_listener(reader, l);                    // ROS2 spin() 的底层
```

## dds_read vs dds_take

| 特性 | `dds_read` | `dds_take` |
|------|-----------|-----------|
| 读取数据 | ✅ | ✅ |
| 移除数据 | ❌ 数据保留在 Reader 缓存 | ✅ 读取后从缓存移除 |
| 重复读取 | ✅ 下次还能读到同一条 | ❌ 读一次就没了 |
| 适用场景 | 状态同步 (需历史) | 事件处理 (一次性) |
| QoS 配合 | 通常用 TransientLocal | 任何 QoS 均可 |
| 内存占用 | 缓存不释放, 持续增长 | 读后释放, 内存稳定 |
| 推荐用法 | 定期轮询检查状态 | Listener 回调中处理事件 |

```c
// dds_read — 保留数据, 适合"当前状态查询"
//   RobotStatus 用 dds_read: 即使错过几条, 仍能读到最新的状态
dds_read(rd, samples, infos, 4, 4);

// dds_take — 消费数据, 适合"事件流处理"  
//   TaskCommand 用 dds_take: 每条命令只处理一次, 避免重复执行
dds_take(rd, samples, infos, 8, 8);
```

## 轮询 (Polling) vs 回调 (Listener)

| 特性 | 轮询 `dds_read` | 回调 `dds_lset_data_available` |
|------|----------------|-------------------------------|
| 模式 | Pull (主动拉取) | Push (被动接收) |
| 延迟 | ~轮询间隔 (如 100ms) | ~微秒级 (立即触发) |
| CPU | 持续唤醒, 空转 | 数据到达才触发, 空闲休眠 |
| 复杂度 | 简单 | 需管理 Listener 生命周期 |
| 对应 ROS2 | 自定义轮询 | `rclcpp::spin()` 底层 |
| 适用场景 | 低频状态查询 | 高频事件/指令处理 |

```
轮询模式:                    回调模式:
  while(running) {              while(running) {
    dds_read()  ← 100ms间隔       sleep(1)    ← 不轮询
    usleep(100000)              }
  }
                                on_data() ← 数据到达立即触发
```

## 0.8.x Loan API 要点

```c
// ★ CycloneDDS 0.8.x 必须使用 void* 指针数组
void* samples[4] = {0};              // 必须零初始化
dds_sample_info_t infos[4];
memset(infos, 0, sizeof(infos));     // 必须零初始化

int n = dds_read(rd, samples, infos, 4, 4);  // 或 dds_take
for (int i = 0; i < n; i++) {
    if (!infos[i].valid_data) continue;
    YourType* data = samples[i];     // void* → 类型指针
    // 使用 data->field
}
if (n > 0) dds_return_loan(rd, samples, n);  // ★ 必须归还 loan
```

---

## 机器人系统 Demo

完整的机器人中控实验，对标 EHR 项目状态机架构：

**架构:**
```
robot_dds_service (服务端)
  TaskCommand Topic ← Listener 回调 (dds_take, 事件模式)
  RobotStatus  Topic → 定时发布 1Hz (状态同步)

robot_dds_monitor (监控端)
  RobotStatus  Topic ← Listener 回调 (dds_take, 实时)
  TaskCommand  Topic ← Listener 回调
  Heartbeat    Topic ← Listener 回调
```

**运行:**
```bash
bash scripts/run_robot_demo.sh    # 一键启动 service + monitor
# 或手动:
./robot_dds_service &
./robot_dds_monitor &
```

**关键升级:**
- 轮询→Listener: 延迟从 ~100ms 降至 ~微秒级
- dds_read→dds_take: 避免重复读取历史数据 (延迟从 ~6000ms 降至 ~0.05ms)
- 退出时打印延迟统计 (avg/max per topic)

## 主流 DDS 实现

| 实现 | 特点 | 许可证 |
|------|------|--------|
| CycloneDDS | 轻量、低延迟, **ROS2 Humble 默认** | Eclipse EPL-2.0 |
| Fast-DDS | 功能丰富, ROS2 Foxy/Galactic 默认 | Apache 2.0 |
| OpenDDS | C++ CORBA 风格 | 自定义开源 |
| RTI Connext | 商业版, 功能最全 | 商业 |

## 与 ROS2 的关系

```
ROS2 Node (rclcpp)
       │
    rmw 层 (抽象)
       │
  CycloneDDS / Fast-DDS  ← 你现在学的就是这个底层
```

学习路线: **DDS 实验 → 用 rclcpp 写 ROS2 节点 → 跑 dds_bridge 验证**

## 进阶方向

- [x] QoS 组合实验
- [x] 多节点自动发现
- [x] Listener 回调模式
- [x] Partition 隔离
- [x] Content Filter 数据过滤
- [x] DDS Security 认证加密
- [x] Key 字段 + Instance 管理
- [x] 延迟/吞吐量 benchmark
