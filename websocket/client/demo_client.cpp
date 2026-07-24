/**
 * demo_client.cpp — WebSocket 信令客户端 Demo
 *
 * 完整演示生产环境语音交互 RTSP 链路的通用化版本:
 *   1. 手写 WebSocket 客户端 → 信令握手 (session_init → session_ready)
 *   2. 模拟数据推送器 (DataPublisher) → 带 ring buffer + 状态机
 *   3. 模拟数据接收器 (DataReceiver) → 拉流 + 重连
 *   4. 事件处理: process_result / data_notify / stream_interrupt 等
 *   5. 心跳保活 + 优雅退出
 *
 * 链路: WS 信令 (控制面) + 模拟数据流 (数据面)
 */

#include <cmath>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "data_publisher.h"
#include "data_receiver.h"
#include "ws_signaling_client.h"

#include "common/ws_common.h"

// ============================================================================
// 全局状态
// ============================================================================
std::unique_ptr<WsSignalingClient> g_client;
std::unique_ptr<DataPublisher> g_publisher;
std::unique_ptr<DataReceiver> g_receiver;

std::string g_session_id;
std::atomic<bool> g_suppressed{false};  // 模拟 VAD 抑制
std::atomic<bool> g_running{true};

void on_signal(int) {
  std::cout << "\n🛑 收到退出信号..." << std::endl;
  g_running.store(false);
}

// ============================================================================
// 模拟数据生成器 (代替真实麦克风)
// ============================================================================
void simulate_data_feed() {
  std::cout << "[DATA] 开始模拟数据生成 (20Hz, 正弦波)..." << std::endl;

  int tick = 0;
  while (g_running.load()) {
    if (!g_publisher || !g_publisher->IsRunning()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    // 生成 800 字节模拟数据 (~20ms @ 16kHz mono S16LE = 640 bytes, 取整)
    uint8_t buf[800];
    for (int i = 0; i < 800; i++) {
      buf[i] = static_cast<uint8_t>(
          128 + 127 * sin(2 * M_PI * 440 * tick / 20.0 + i * 0.1));
    }
    g_publisher->FeedData(buf, sizeof(buf));

    tick++;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));  // 20Hz
  }
  std::cout << "[DATA] 数据生成停止" << std::endl;
}

// ============================================================================
// 服务器消息处理
// ============================================================================
void handle_server_message(const std::string& message) {
  std::string event = JsonBuilder::extract_event(message);

  if (event == WsEvent::kChannelReady) {
    std::cout << "  ✅ 服务器确认通道就绪" << std::endl;

  } else if (event == WsEvent::kProcessResult) {
    std::string progress = JsonBuilder::extract_data_field(message, "progress");
    std::string text = JsonBuilder::extract_data_field(message, "text");
    std::cout << "  📊 处理结果: progress=" << progress
              << "%, text=" << text << std::endl;

  } else if (event == WsEvent::kInferenceResult) {
    std::string reply = JsonBuilder::extract_data_field(message, "reply");
    std::string confidence = JsonBuilder::extract_data_field(message, "confidence");
    std::cout << "  🤖 推理结果: reply=" << reply
              << ", confidence=" << confidence << std::endl;

  } else if (event == WsEvent::kDataNotify) {
    std::string data_id = JsonBuilder::extract_data_field(message, "data_id");
    std::string data_url = JsonBuilder::extract_data_field(message, "data_url");
    std::string size_bytes = JsonBuilder::extract_data_field(message, "size_bytes");
    std::cout << "  📢 数据通知: data_id=" << data_id
              << ", size=" << size_bytes << " bytes"
              << ", url=" << data_url << std::endl;

    // 启动数据接收器 (模拟拉流)
    if (!g_receiver->IsRunning()) {
      g_receiver->Initialize(data_url);
      g_receiver->SetOneShot(true);  // HTTP 文件模拟
      g_receiver->Start();

      // 通知服务器: 开始处理 (stream_state=started, 抑制服务器侧处理)
      if (g_client) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        g_client->NotifyStreamState(g_session_id, "started");
        g_suppressed.store(true);
      }
    }

  } else if (event == WsEvent::kDataComplete) {
    std::cout << "  🏁 数据完成" << std::endl;
    // 通知服务器: 处理完成 (stream_state=completed, 恢复服务器侧处理)
    if (g_client) {
      g_client->NotifyStreamState(g_session_id, "completed");
      g_suppressed.store(false);
    }

  } else if (event == WsEvent::kDataCancel) {
    std::cout << "  ⏹️ 数据取消" << std::endl;
    if (g_receiver) g_receiver->Stop();
    if (g_client) {
      g_client->NotifyStreamState(g_session_id, "completed");
      g_suppressed.store(false);
    }

  } else if (event == WsEvent::kStreamInterrupt) {
    std::string reason = JsonBuilder::extract_data_field(message, "reason");
    std::cout << "  ⚡ 流中断: reason=" << reason << std::endl;

    // 停止接收
    if (g_receiver) g_receiver->Stop();

    // 确认中断完成
    if (g_client) {
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      g_client->NotifyStreamState(g_session_id, "completed");
      g_suppressed.store(false);
    }

  } else if (event == WsEvent::kTipMessage) {
    std::string content = JsonBuilder::extract_data_field(message, "content");
    std::cout << "  💬 提示: " << content << std::endl;

  } else {
    std::cout << "  ❓ 未知事件: " << event << std::endl;
  }
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char* argv[]) {
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  std::string ws_url = WsConfig::kDefaultUrl;
  if (argc > 1) ws_url = argv[1];

  std::cout << "========================================" << std::endl;
  std::cout << "  WebSocket 信令客户端 Demo" << std::endl;
  std::cout << "  目标: " << ws_url << std::endl;
  std::cout << "  链路: WS 信令 + 模拟数据推拉流" << std::endl;
  std::cout << "========================================\n" << std::endl;

  // ---- 1. 创建信令客户端 ----
  g_client = std::make_unique<WsSignalingClient>();
  g_client->SetTimeoutConfig(WsConfig::kDefaultHeartbeatSec,
                              WsConfig::kDefaultConnectTimeoutMs,
                              WsConfig::kDefaultIoTimeoutSec);

  g_client->SetMessageCallback(
      [](const std::string& message) { handle_server_message(message); });

  g_client->SetDisconnectCallback([]() {
    std::cout << "[MAIN] 连接断开回调" << std::endl;
    g_running.store(false);
  });

  // ---- 2. 连接 + 会话握手 ----
  WsSignalingClient::SessionInfo session;
  std::string error;
  if (!g_client->Connect(ws_url, WsConfig::kDefaultClientId,
                          WsConfig::kDefaultChannel, session, error)) {
    std::cerr << "❌ 连接失败: " << error << std::endl;
    return 1;
  }
  g_session_id = session.session_id;

  std::cout << "\n✅ 会话建立:" << std::endl;
  std::cout << "   session_id:   " << session.session_id << std::endl;
  std::cout << "   data_channel: " << session.data_channel << std::endl;
  std::cout << "   client_id:    " << session.client_id << std::endl;
  std::cout << "   channel:      " << session.channel << std::endl;

  // ---- 3. 创建数据推送器和接收器 ----
  g_publisher = std::make_unique<DataPublisher>();
  g_publisher->Initialize(session.data_channel);

  g_receiver = std::make_unique<DataReceiver>();

  // ---- 4. 启动数据推送 (模拟 ffmpeg 推流) ----
  std::cout << "\n--- 启动数据推送 ---" << std::endl;
  g_publisher->Start();

  // 等待推送器就绪
  PushStreamState state = g_publisher->WaitForStreamReady(5000);
  if (state != PushStreamState::Streaming) {
    std::cerr << "❌ 推送器启动失败" << std::endl;
    g_client->Disconnect();
    return 1;
  }
  std::cout << "✅ 推送器就绪" << std::endl;

  // ---- 5. 通知服务器开始推流 ----
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  int64_t push_time = g_publisher->GetStartedMs();
  if (!g_client->NotifyStreamBegin(g_session_id, push_time)) {
    std::cerr << "❌ 发送 stream_begin 失败" << std::endl;
  }

  // ---- 6. 启动数据生成线程 ----
  std::thread data_thread(simulate_data_feed);

  // ---- 7. 主循环: 监控 DataReceiver 完成并发送 stream_state ----
  std::cout << "\n--- 运行中 (Ctrl-C 退出) ---\n" << std::endl;

  while (g_running.load() && g_client->IsConnected()) {
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 检查 receiver 是否完成了 One-shot 拉取
    if (g_receiver && !g_receiver->IsRunning() &&
        g_suppressed.load()) {
      std::cout << "[MAIN] 检测到数据接收完成, 发送 stream_state(completed)" << std::endl;
      g_client->NotifyStreamState(g_session_id, "completed");
      g_suppressed.store(false);
    }

    // 定期打印统计
    if (g_publisher && g_publisher->IsRunning()) {
      std::cout << "[MAIN] 推送器状态: " << static_cast<int>(g_publisher->GetState())
                << ", 接收器: " << (g_receiver && g_receiver->IsRunning() ? "运行中" : "空闲")
                << ", 接收字节: " << (g_receiver ? g_receiver->GetBytesReceived() : 0)
                << std::endl;
    }
  }

  // ---- 8. 优雅退出 ----
  std::cout << "\n--- 开始退出 ---" << std::endl;

  g_running.store(false);
  if (data_thread.joinable()) data_thread.join();

  if (g_publisher) g_publisher->Stop();
  if (g_receiver) g_receiver->Stop();

  if (g_client) {
    // 最后发一次 completed 确保服务器恢复
    if (g_suppressed.load()) {
      g_client->NotifyStreamState(g_session_id, "completed");
    }
    g_client->Disconnect();
  }

  std::cout << "✅ 客户端已退出" << std::endl;
  return 0;
}
