# ros2_zenoh_lab — RMW 无关的传输自省节点

一个节点,一条命令,自报"我现在跑在哪条传输栈上"。

## 为什么只有这一个文件

`../README.md` 里的四个 Demo(A 切换 / B 桥接 / C 跨域 / D 共存)全部复用
`src/rmw_probe_node.cpp` 一个可执行文件。角色差异靠参数与重映射表达,不靠多写代码:

```bash
# 机器人侧遥测发布
rmw_probe_node --ros-args -p mode:=pub -p topic:=/fleet/telemetry -p label:=robot_telemetry

# 云端监控订阅
rmw_probe_node --ros-args -p mode:=sub -p topic:=/fleet/telemetry -p label:=cloud_monitor

# 换个节点名 (等价于 -r __node:=...)
rmw_probe_node --ros-args -p mode:=sub -r __node:=dds_peer
```

## 参数

| 参数 | 默认 | 环境变量兜底 | 说明 |
|------|------|------------|------|
| `mode` | `pub` | `ZENOH_LAB_MODE` | `pub` / `sub` / `both` |
| `topic` | `/zenoh_probe` | `ZENOH_LAB_TOPIC` | 话题名,自动补前导 `/` |
| `rate_hz` | `2.0` | `ZENOH_LAB_RATE_HZ` | 发布频率 |
| `label` | 节点名 | `ZENOH_LAB_LABEL` | 横幅与载荷里的角色标签 |
| `exit_after` | `0` (不限) | — | N 秒后自动退出并打印 `SUMMARY` |

**为什么要有环境变量兜底**:参数注入 (`-p`) 在 ROS2 里走 service,而 `rmw_zenoh_cpp`
在 Humble 上有"高负载时 service 调用被静默丢弃"的已知问题。脚本在 zenoh 场景下会
同时设 `-p` 和 `ZENOH_LAB_*`,参数丢了也不至于让 Demo 失败。

## 输出约定

退出时一定打印一行 `SUMMARY`,供脚本断言:

```
SUMMARY mode=sub topic=/fleet/telemetry sent=0 received=23 peer_rmw=rmw_fastrtps_cpp rmw=rmw_fastrtps_cpp domain=1
```

`../scripts/run_demo.sh` 就是靠 `received=` 这个字段做"跨域时云侧应收到 0 条"的负对照。

## 本包不做的事

- **不链接任何 DDS/Zenoh 库**。换传输纯靠 `RMW_IMPLEMENTATION` 环境变量,这正是 RMW
  抽象层的意义所在 —— 也正因如此,`package.xml` 里只有 `rclcpp`/`std_msgs`/`rmw_implementation`
  三个依赖。
- **不实现桥接**。桥接是 `zenoh_bridge_dds` 这个独立二进制的职责,见 `../scripts/run_demo.sh`。
