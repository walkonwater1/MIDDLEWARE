# gRPC — 高性能 RPC 框架

> 基于 HTTP/2 + Protobuf 的多模式 RPC 框架，Google 开源，云原生标准

## 动机

车载 SOA 中 DDS 负责传感器数据分发，但 ECU 间的方法调用（"请执行自检"、"查询诊断码"）用 RPC 更自然。gRPC 提供 IDL 定义接口 → 自动生成类型安全的 Client/Server 桩代码 → HTTP/2 多路复用 + 双向流。AUTOSAR Adaptive 已将 gRPC 纳入标准。

## 通信模式

gRPC 支持四种模式，本 Demo 演示前三种：

| 模式 | 方向 | 场景 |
|------|------|------|
| **Unary RPC** | Client → Request, Server → Response | 查询机器人状态、执行诊断 |
| **Server Streaming** | Client → Request, Server → stream | 订阅遥测数据、日志推送 |
| **Client Streaming** | Client → stream, Server → Response | 批量上报任务进度、上传文件 |
| **Bidirectional** | stream ↔ stream (未演示) | 实时对话、协同控制 |

```
┌─────────────────────────────────────┐
│         gRPC RobotService           │
│                                     │
│  Server (server.cpp)                │
│  ┌─────────────────────────────┐    │
│  │ RobotSimulator (6 轴机械臂)  │    │
│  │  - 关节位置/速度/扭矩       │    │
│  │  - 电池/CPU/内存            │    │
│  │  - 任务进度模拟             │    │
│  └─────────────────────────────┘    │
│            │                        │
│     ┌──────┼──────┬───────────┐     │
│     ▼      ▼      ▼           ▼     │
│  Unary  Server  Client   Bidirect. │
│  RPC    Stream  Stream   Stream    │
│            │                        │
│  HTTP/2 (multiplexed, binary)       │
│            │                        │
│  Client (client.cpp)                │
│  ┌─────────────────────────────┐    │
│  │ 1. GetStatus (Unary)        │    │
│  │ 2. StreamTelemetry (5 条)   │    │
│  │ 3. ReportTaskProgress (5 次)│    │
│  └─────────────────────────────┘    │
└─────────────────────────────────────┘
```

## 快速开始

```bash
# 安装依赖 (仅首次)
sudo apt-get install -y libgrpc++-dev protobuf-compiler-grpc

# 只编译 (Debug / release / clean)
bash scripts/build.sh release

# 一键编译 + 运行 (服务端后台 + 客户端前台)
bash scripts/run_demo.sh

# 或手动分步:
bash scripts/run_demo.sh server    # 终端 1 — 服务端
bash scripts/run_demo.sh client    # 终端 2 — 客户端

# 自定义端口 / 远程连接:
GRPC_PORT=50052 bash scripts/run_demo.sh          # 自定义端口
GRPC_HOST=192.168.1.100 bash scripts/run_demo.sh client  # 远程服务端
```

## 手动构建

```bash
cd grpc && mkdir -p build && cd build
cmake .. && make -j$(nproc)

# 运行
./grpc-server &
./grpc-client
```

**预期输出:**

Server 端持续更新 6 轴关节、电池、CPU 数据。Client 端依次演示:

1. **Unary RPC** — 查询一次完整状态（关节角度/速度/扭矩/温度 + 当前任务）
2. **Server Streaming** — 接收 5 条遥测推送后主动断开
3. **Client Streaming** — 分 5 次上报任务进度 (20% → 40% → ... → 100%)

## Proto 定义

```protobuf
service RobotService {
  rpc GetStatus(GetStatusRequest) returns (GetStatusResponse);          // Unary
  rpc StreamTelemetry(StreamTelemetryRequest) returns (stream TelemetrySample); // Server→Client 流
  rpc ReportTaskProgress(stream TaskReport) returns (TaskSummary);      // Client→Server 流
}
```

## 与其他中间件对比

| 维度 | gRPC | DDS | SOME/IP | MQTT |
|------|------|-----|---------|------|
| 通信范式 | RPC + Stream | Pub/Sub | RPC + Event | Pub/Sub |
| IDL | Protobuf | IDL (.idl) | Franca/ARXML | 无(自定义) |
| 传输层 | HTTP/2 (TCP) | UDP 组播 | UDP/TCP | TCP |
| 服务发现 | DNS/NameResolver | SPDP/SEDP | SOME/IP-SD | Broker 地址 |
| QoS | 无(DNS 重试) | 20+ QoS 策略 | SOME/IP-TP | QoS 0/1/2 |
| 实时性 | 软实时 | 硬实时(QoS) | 软实时 | 无保证 |
| 适用场景 | 云原生/微服务 | 自动驾驶/机器人 | 车载 ECU | IoT/车联网 |

## 进阶学习方向

- [ ] Bidirectional Streaming: `stream Message` returns `stream Message`
- [ ] 异步 Client/Server (Async API)
- [ ] TLS/mTLS 安全传输
- [ ] gRPC 拦截器 (Interceptor): 日志/认证/限流
- [ ] gRPC Reflection: 服务自描述
- [ ] gRPC ↔ DDS 桥接 (gRPC 面向云, DDS 面向车内)
- [ ] 与 REST API 对比: 1 个 gRPC 服务 = N 个 REST 端点
- [ ] 负载均衡: Client-Side LB vs Proxy LB

## 参考资料

- [gRPC 官方文档](https://grpc.io/docs/languages/cpp/)
- [Protobuf 语言指南](https://protobuf.dev/programming-guides/proto3/)
- [gRPC vs REST 对比](https://cloud.google.com/blog/products/api-management/understanding-grpc-openapi-and-rest)
