# EtherCAT — 工业实时以太网现场总线

> 机器人高精度多轴同步控制的标准总线

## 定位：从总线到中间件

EtherCAT 本身是 **数据链路层协议**，但其上层协议栈实现了丰富的中间件抽象：

| 层 | 协议/功能 | 中间件类比 |
|----|----------|-----------|
| **CoE (CANopen over EtherCAT)** | 对象字典 + PDO/SDO | 完全等价于 CANopen 的 OD 模型 |
| **FoE (File over EtherCAT)** | 固件升级 | 远程文件传输 |
| **EoE (Ethernet over EtherCAT)** | TCP/IP 隧道 | 标准网络栈复用 |
| **SoE (Sercos over EtherCAT)** | IDN 参数访问 | 伺服驱动专用配置 |
| **DC (Distributed Clock)** | 分布式时钟同步 | 纳秒级多节点同步 |

## 核心概念

### 帧结构
```
Ethernet Frame
├─ MAC Header (DA=广播, SA=主站, Type=0x88A4)
├─ EtherCAT Header (长度 + 类型)
├─ Datagram 1 (寻址 + 命令 + 数据)
├─ Datagram 2
├─ ...
└─ FCS (CRC32)
```

### 数据报命令 (Datagram)

`ethercat.h` 定义了 EtherCAT 全部数据报命令，`ethercat.c` 的 `virtual_esc_process_frame()` 在从站侧逐条处理：

| 命令 | 码 | 解析器处理 | 寻址方式 |
|------|----|-----------|---------|
| NOP | 0x00 | ✓ (恒成功) | — |
| APRD / APWR | 0x01 / 0x02 | ✓ | 自增位置 |
| FPRD / FPWR | 0x04 / 0x05 | ✓ | 固定地址 |
| BRD / BWR | 0x07 / 0x08 | ✓ | 广播 |
| LRD / LWR | 0x0A / 0x0B | ✓ | 逻辑寻址 (过程数据) |
| APRW / FPRW / LRW | 0x03 / 0x06 / 0x0C | 仅枚举定义 | 读写合并 |
| ARMW / FRMW | 0x0D / 0x0E | 仅枚举定义 | 多帧 (DC 时钟) |

每次处理完在响应帧末尾追加 **WKC (Working Counter)**：成功处理的子操作数，主站据此校验数据报是否被从站真正读取/写入。主站每个周期构造一条帧，内含 **2 个数据报**：`LWR → SM2` (写 3 轴目标位置) 与 `LRD ← SM3` (读 3 轴实际位置)，响应里逐条取回 WKC 累加。

### "飞读飞写" (On-the-Fly)
每个从站不存储整个帧，而是边接收边处理：
- 读命令: 在数据流经时向指定位置插入数据
- 写命令: 从指定位置取出数据并更新硬件

### 状态机 (ESM — EtherCAT State Machine)
```
Init → Pre-Operational → Safe-Operational → Operational
  ↑         ↓                ↓                  ↓
  └─────────┴────────────────┴──────────────────┘
                   (可回退)
```

| 状态 | 邮箱 (CoE) | PDO | DC | 说明 |
|------|-----------|-----|----|------|
| Init | ✗ | ✗ | ✗ | 仅寄存器访问 |
| Pre-Op | ✓ | ✗ | ✗ | 邮箱配置 |
| Safe-Op | ✓ | 输入 | ✗ | 可读过程数据 |
| Op | ✓ | ✓ | ✓ | 全速运行 |

### 分布式时钟 (DC)
- 第1个支持 DC 的从站作为参考时钟
- 其他从站计算与参考时钟的偏移
- 通过发送 ARMW 命令补偿时钟漂移
- 典型同步精度: **< 100ns**

## 本 Demo 架构

```
┌──────────────────────────────────────────────┐
│           ethercat_master.c                  │
│  模拟 SOEM 风格 API:                          │
│  - ec_init()   扫描从站                       │
│  - ec_config() 配置 PDO 映射 + FMMU + SM     │
│  - ec_dcsync() 分布式时钟同步                 │
│  - ec_send_processdata()  发送输出/接收输入    │
│  - ec_receive_processdata()                  │
│  - CoE SDO 读写                              │
└──────────┬───────────────────────────────────┘
           │  (共享内存模拟 ESC)
┌──────────┴───────────────────────────────────┐
│    ethercat_slave.c  (虚拟从站 #1)           │
│  - ESC 寄存器: DL状态/AL控制/WKC/SM/FMMU     │
│  - 过程数据: 模拟 3 轴位置 (I32×3)            │
│  - DC: 时间戳 + 漂移补偿                      │
│  - CoE: 1C12/1C13 (SM分配), 1600/1A00 (PDO)  │
└──────────────────────────────────────────────┘

周期: 1kHz (1ms), 1 个从站 / 3 个伺服轴, 仿真运行 15 秒
过程数据 (见 setup_slave_pdo):
  TxPDO (从站→主站, SM3) = 3×I32 实际位置 + I16 状态字 = 14 字节  (0x1A00, 4 条目)
  RxPDO (主站→从站, SM2) = 3×I32 目标位置              = 12 字节  (0x1600, 3 条目)
```

## 编译运行

```bash
# 方式一: 一键脚本 (自动编译 + 从站后台 + 主站前台)
cd ethercat && bash scripts/run_demo.sh both
# 其他子命令: slave / master / build / clean
```

```bash
# 方式二: 手动分步
sudo apt install cmake gcc

cd ethercat && mkdir build && cd build && cmake .. && make

./ethercat_slave    # 终端1: 启动虚拟从站
./ethercat_master   # 终端2: 启动主站
```

## 预期输出

```
=== EtherCAT Virtual Slave #1 ===           ← 终端1
Type: Beckhoff EL6521 (3-axis servo simulator)
[SLAVE#1] ESC initialized
[SLAVE#1] FMMU=8, SM=8, RAM=8KB
[SLAVE#1] CoE Objects:
          [1000] Device Type                    = 0x00020192
          [1001] Error Register                 = 0x00000000
          [1C00] Sync Manager Type count        = 0x00000004
          [1C12] RxPDO Assign count             = 0x00000001
          [1C13] TxPDO Assign count             = 0x00000001
          [1600] RxPDO Map count                = 0x00000003
          [1A00] TxPDO Map count                = 0x00000004
[SLAVE#1] Waiting for master...
  [ESC] 状态转换: INIT → PREOP

=== EtherCAT Master (SOEM-style) ===        ← 终端2
[MASTER] Connected to slave via shared memory
[MASTER] Initializing EtherCAT master...
[MASTER] Configuring 1 slave(s)...
[MASTER]   Slave #1: 0x00000002/0x06520000
[MASTER] Mapping PDO for slave #1...
[MASTER]   RxPDO: 12 bytes (SM2)
[MASTER]   TxPDO: 14 bytes (SM3)
[MASTER] Configuring Distributed Clock...
[MASTER]   Sync0 cycle: 1000000 ns (1.00 kHz)
[MASTER]   Sync0 shift: 1000000 ns
[MASTER] Slave #1 → INIT
[MASTER] Slave #1 → PREOP
[MASTER] Slave #1 → SAFEOP
[MASTER] CoE SDO Read 0x1C13 (TxPDO assign)...
[MASTER] Slave #1 → OP

[MASTER] Entering cyclic mode (1kHz)...
[MASTER] ┌────────┬──────────┬──────────┬──────────┬──────────┬──────────┐
[MASTER] │ cycle  │ axis0    │ axis1    │ axis2    │ wkc      │ dc_diff  │
[MASTER] ├────────┼──────────┼──────────┼──────────┼──────────┼──────────┤
[MASTER] │    100 │     1024 │     2048 │     3072 │ 0x0002   │      +52 │
[MASTER] │    200 │     1124 │     1948 │     2960 │ 0x0002   │      -31 │
...

[SLAVE#1] State=OP | WKC=0x0002 | Pos=[1024, 2048, 3072]
```

## 与其它中间件的结合场景

| 场景 | 组合 | 说明 |
|------|------|------|
| 机器人控制 | EtherCAT → ROS2 | ros2_control + EtherCAT 硬件接口 |
| 整车架构 | EtherCAT ← CANopen | CoE 对象字典复用 CANopen 模型 |
| 传感器融合 | EtherCAT → DDS | 高频传感器数据进入去中心化数据总线 |
| 跨域通信 | EtherCAT → SOME/IP | 车内实时域 ↔ 信息域网关 |

## 关键文件

| 文件 | 说明 |
|------|------|
| `ethercat.h` | ESC 寄存器地址、数据报命令码、ESM 状态机、PDO 结构、DC 配置 |
| `ethercat.c` | 共享核心：`virtual_esc_process_frame()` 数据报解析 + `ecx_coe_sdo_read/write()` CoE SDO |
| `ethercat_slave.c` | 虚拟从站：ESC 寄存器模拟 / PDO 数据 / DC 时钟 / CoE 对象字典 |
| `ethercat_master.c` | SOEM 风格主站：扫描 / 配置 / DC 同步 / 周期交换 / WKC 校验 / CoE 对象读取 |
| `scripts/run_demo.sh` | 一键编译运行 (both / slave / master) |

## 进阶学习方向

- [ ] ESC 寄存器手册（Beckhoff ET1100 / ET1200）精读
- [ ] SOEM 源码分析：`ecx_send_processdata()` 内部实现
- [ ] 分布式时钟 (DC) 漂移补偿算法
- [ ] FMMU (Fieldbus Memory Management Unit) 映射原理
- [ ] SyncManager 配置：Buffered vs Mailbox 模式
- [ ] CoE 协议的 SDO 信息区与分段传输
- [ ] 多轴轨迹规划 + EtherCAT 同步插补
- [ ] IgH EtherCAT Master 对比分析
- [ ] 与 Linux PREEMPT_RT / Xenomai 实时内核配合

## 参考资源

- Beckhoff ETG.1000 — EtherCAT 技术规范
- [SOEM](https://github.com/OpenEtherCATsociety/SOEM) — Simple Open EtherCAT Master
- [IgH EtherCAT Master](https://etherlab.org/en/ethercat/) — Linux 主站实现
- Beckhoff Application Note: DC Distributed Clocks
