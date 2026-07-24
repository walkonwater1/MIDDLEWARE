# WebSocket 信令 + 数据流 Demo

基于 RFC 6455 的 WebSocket 通信中间件演示，模拟生产环境中 **WebSocket 信令 + 数据推拉流** 的完整链路。

## 架构

```
┌─────────────────────────────────────────────────────────┐
│                    WebSocket 信令                        │
│  Client                              Server             │
│  ┌──────────────┐   session_init     ┌──────────────┐  │
│  │ Signaling    │ ←───────────────→ │ Signaling    │  │
│  │ Client       │   session_ready    │ Server       │  │
│  │ (手写RFC6455)│   stream_begin     │ (websocketpp)│  │
│  └──────┬───────┘   data_notify      └──────┬───────┘  │
│         │           stream_interrupt        │           │
│         │           process_result          │           │
│         │           heartbeat               │           │
└─────────┼───────────────────────────────────┼───────────┘
          │                                   │
   ┌──────┴───────┐                    ┌──────┴───────┐
   │ Data         │   (模拟数据推送)     │ Data         │
   │ Publisher    │                    │ Processor    │
   │ (ring buffer │                    │ (处理+存储)   │
   │  + 后台线程)  │                    └──────────────┘
   └──────────────┘
   ┌──────────────┐
   │ Data         │   (模拟数据拉取)
   │ Receiver     │
   │ (后台线程)    │
   └──────────────┘
```

## 目录结构

```
websocket/
├── common/
│   ├── ws_common.h          # 共享: 事件常量、JSON 构建器
│   └── ring_buffer.h        # 线程安全环形缓冲区
├── client/
│   ├── ws_signaling_client.h/.cpp  # 手写 RFC 6455 客户端
│   ├── data_publisher.h/.cpp       # 数据推送器 (ring buffer + 状态机)
│   ├── data_receiver.h/.cpp        # 数据接收器 (拉流 + 重连)
│   └── demo_client.cpp             # 客户端主程序
├── server/
│   ├── ws_signaling_server.h/.cpp  # 信令服务器 (会话管理)
│   ├── data_processor.h/.cpp       # 数据处理引擎 (模拟 ASR/推理)
│   └── demo_server.cpp             # 服务端主程序
├── scripts/
│   └── run_demo.sh           # 一键运行脚本
├── CMakeLists.txt
└── README.md
```

## 快速开始

```bash
# 终端1: 启动服务端
cd websocket && bash scripts/run_demo.sh server

# 终端2: 启动客户端
cd websocket && bash scripts/run_demo.sh client

# 或一键运行 (服务端后台 + 客户端前台)
cd websocket && bash scripts/run_demo.sh
```

自定义端口:
```bash
WS_PORT=8080 bash scripts/run_demo.sh server
WS_PORT=8080 WS_HOST=192.168.1.100 bash scripts/run_demo.sh client
```

## 协议

### 消息格式

所有消息使用统一 JSON 信封:

```json
{
  "event": "<事件类型>",
  "session_id": "<会话ID>",
  "timestamp": 1752758582074,
  "data": { ... }
}
```

### 事件类型

| 事件 | 方向 | 说明 |
|------|------|------|
| `session_init` | C→S | 会话初始化: `data={client_id, channel}` |
| `session_ready` | S→C | 握手响应: `data={data_channel, client_id, channel}` |
| `stream_begin` | C→S | 通知开始推流: `data={push_time}` |
| `channel_ready` | S→C | 通道就绪确认: `data={latency_ms, status}` |
| `stream_state` | C→S | 流状态变更: `data={status: "started"/"completed"}` |
| `process_result` | S→C | 处理结果: `data={progress, text}` |
| `inference_result` | S→C | 推理结果: `data={reply, confidence}` |
| `data_notify` | S→C | 通知数据就绪: `data={data_id, data_url, size_bytes}` |
| `data_cancel` | S→C | 取消当前数据 |
| `data_complete` | S→C | 数据处理完成 |
| `stream_interrupt` | S→C | 中断当前流: `data={reason}` |
| `tip_message` | S→C | 提示信息: `data={content, duration}` |
| `heartbeat` | C→S | 应用层心跳 (每15s) |

### 时序流程

```
[Client]                              [Server]
   │── TCP connect ────────────────────│
   │── HTTP Upgrade (101) ─────────────│
   │── session_init ──────────────────→│  ① 握手获取会话
   │←─ session_ready ──────────────────│
   │                                    │
   │── [启动 DataPublisher]             │
   │── stream_begin ──────────────────→│  ② 通知开始推流
   │←─ channel_ready ──────────────────│
   │                                    │
   │←─ inference_result ──────────────│  ③ 推理结果
   │←─ process_result (×N) ───────────│  ④ 处理进度
   │←─ tip_message ───────────────────│
   │                                    │
   │←─ data_notify ───────────────────│  ⑤ 数据就绪
   │── [启动 DataReceiver]              │
   │── stream_state(started) ────────→│     暂停服务器处理
   │   [DataReceiver 接收完成]          │
   │── stream_state(completed) ──────→│     恢复服务器处理
   │                                    │
   │←─ stream_interrupt ──────────────│  ⑥ 中断
   │── stream_state(completed) ──────→│     确认中断完成
   │                                    │
   │── heartbeat (每15s) ────────────→│  ⑦ 保活
```

## 核心特性

### 1. 手写 WebSocket 客户端 (RFC 6455)

`ws_signaling_client` 从零实现 WebSocket 协议:

- **TCP 连接**: 非阻塞 connect + poll 超时 + `SO_ERROR` 检查
- **HTTP 升级**: 标准 `GET` + `Upgrade: websocket` + `Sec-WebSocket-Key`
- **帧编解码**: 文本帧 (opcode 0x1)、关闭帧 (0x8)、Ping/Pong (0x9/0xA)
- **掩码**: 客户端→服务器所有帧使用随机 4 字节掩码
- **心跳**: JSON 文本帧 `{"event":"heartbeat"}` 每 15s (绕过 uvicorn 的 Ping 不计时限制)
- **线程安全**: `std::timed_mutex` 保护发送路径

### 2. 数据推送器 (DataPublisher)

- **Ring Buffer**: SPSC 无锁环形缓冲，128K 容量
- **状态机**: `Idle → Launching → Streaming → Reconnecting → Error`
- **重连**: 指数退避 (2s → 30s cap)
- **线程模型**: 生产者 (FeedData) + 消费者 (PushLoop) 分离

### 3. 数据处理引擎 (DataProcessor)

- 模拟 ASR → LLM → TTS 处理管线
- 周期发送 `process_result` + `inference_result`
- `stream_state(started/completed)` 控制处理暂停/恢复

## 依赖

- **websocketpp** (header-only): `libwebsocketpp-dev`
- **Boost.System**: `libboost-system-dev`
- **C++17** (gcc ≥ 8 / clang ≥ 7)

```bash
sudo apt-get install -y libwebsocketpp-dev libboost-system-dev
```

## 编译

```bash
cd websocket
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## 与其他中间件对比

| 特性 | WebSocket | MQTT | gRPC | DDS |
|------|-----------|------|------|-----|
| 传输协议 | TCP (HTTP Upgrade) | TCP | HTTP/2 | UDP/TCP |
| 通信模式 | 全双工 | Pub/Sub | RPC + Stream | Pub/Sub |
| 浏览器支持 | ✅ 原生 | ✅ (ws) | ❌ | ❌ |
| 帧开销 | 2-14 bytes | 2 bytes | 5+ bytes | 可变 |
| 适合场景 | Web 实时、信令 | IoT、遥测 | 微服务 | 机器人 ROS2 |

## 参考

- [RFC 6455 — The WebSocket Protocol](https://datatracker.ietf.org/doc/html/rfc6455)
- [websocketpp 文档](https://docs.websocketpp.org/)
- [MDN WebSocket API](https://developer.mozilla.org/en-US/docs/Web/API/WebSocket)
