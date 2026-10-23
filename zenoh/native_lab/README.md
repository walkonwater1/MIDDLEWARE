# native_lab — 原生 Zenoh API 示例

用 **zenoh-cpp** 直接写 Zenoh 程序,不涉及 ROS2。目的只有一个:
在把 Zenoh 接到 ROS2 之前,先看清楚 Zenoh 自己的 pub/sub 与 query 长什么样。

完整说明与实测输出见 [../README.md](../README.md) 的 **Demo 0**。

## 五个程序

| 文件 | 演示 | 关键 API |
|------|------|---------|
| [z_pub.cpp](src/z_pub.cpp) | 发布端 | `Config::create_default` → `Session::open` → `declare_publisher` → `put` |
| [z_sub.cpp](src/z_sub.cpp) | 订阅端 | `declare_subscriber(key, on_sample, on_drop)`,key expression 通配 |
| [z_queryable.cpp](src/z_queryable.cpp) | 查询服务端 | `declare_queryable` → `query.reply` |
| [z_get.cpp](src/z_get.cpp) | 查询客户端 | `get` + `FifoChannel`,收集有限条 reply |
| [z_sysmon.cpp](src/z_sysmon.cpp) | **系统状态采集端** | 读 `/proc/stat`、`/proc/meminfo`、`/sys/class/thermal`,按周期发布三路 key |

`z_sysmon` 是给 H618 这类设备节点用的真实采集程序(不是模拟值)。它每个采样点
发布三条消息,共用同一个 `seq` 与 `ts_ns`,方便订阅端对齐与统计延迟:

```
h618/system/cpu          usage_pct / load1..15 / procs / uptime_s
h618/system/memory       total_mb / available_mb / used_mb / usage_pct
h618/system/temperature  max_c + 所有 thermal zone 的逐项温度
```

```bash
./build/z_sysmon                               # 1 Hz 一直发
./build/z_sysmon h618/system 1 10              # 采 10 个点
./build/z_sysmon h618/system 1 0 tcp/192.168.10.1:7447   # client 模式
```

跨主机实测见 [../cross_host/README.md](../cross_host/README.md)。

[common.hxx](src/common.hxx) 是公共小工具:统一打开 Session(peer / client)、
Ctrl-C 退出、打印转义。**刻意没有封装 pub/sub/query 本身** —— 那几步是示例的主角。

## 编译与运行

```bash
# 编译 (产物在 build/)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# 发布 / 订阅 (peer 模式, 无需路由器)
./build/z_sub demo/hello 8 &
./build/z_pub demo/hello 5 15

# Query / Queryable
./build/z_queryable 'demo/robot/**' 20 &
./build/z_get 'demo/robot/**' 2000

# client 模式: 最后一个参数是路由器端点
./build/z_sub demo/router 8 tcp/localhost:7447
```

或者用编排脚本一次跑完:

```bash
bash ../scripts/run_native.sh all
```

## 依赖从哪来

不需要单独安装 Zenoh。ROS2 Humble 的 `ros-humble-rmw-zenoh-cpp` 已经装了:

```
/opt/ros/humble/opt/zenoh_cpp_vendor/
├── include/zenoh.hxx, zenoh/     # zenoh-cpp 头文件 (header-only)
└── lib/libzenohc.so              # zenoh-c 1.8.0
```

`CMakeLists.txt` 默认用这个前缀;换机器/自己编译了 zenoh-c 时用
`-DZENOH_PREFIX=/your/prefix` 覆盖。
