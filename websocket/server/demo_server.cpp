/**
 * demo_server.cpp — WebSocket 信令服务器 Demo
 *
 * 完整演示:
 *   1. 信令服务器 (ws_signaling_server) — 连接管理、会话创建、事件路由
 *   2. 数据处理引擎 (data_processor) — 模拟 ASR/推理/TTS 管线
 *   3. 优雅退出 (SIGINT/SIGTERM)
 */

#include <csignal>
#include <iostream>
#include <memory>
#include <string>

#include "data_processor.h"
#include "ws_signaling_server.h"

#include "common/ws_common.h"

// ============================================================================
// 全局指针 (信号处理用)
// ============================================================================
std::unique_ptr<WsSignalingServer> g_server;
std::unique_ptr<DataProcessor> g_processor;

void on_signal(int) {
  std::cout << "\n🛑 收到退出信号..." << std::endl;
  if (g_processor) g_processor->Stop();
  if (g_server) g_server->Stop();
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char* argv[]) {
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  uint16_t port = WsConfig::kDefaultPort;
  if (argc > 1) port = static_cast<uint16_t>(std::stoi(argv[1]));

  // 1. 创建信令服务器
  g_server = std::make_unique<WsSignalingServer>();

  // 2. 创建数据处理引擎
  g_processor = std::make_unique<DataProcessor>();
  g_processor->SetSendCallback(
      [](const std::string& session_id, const std::string& message) {
        if (g_server) g_server->SendToSession(session_id, message);
      });

  // 3. 绑定信令服务器回调 → 数据处理引擎
  g_server->SetMessageCallback(
      [](connection_hdl hdl, const std::string& session_id,
         const std::string& event, const std::string& payload) {
        if (!g_processor) return;

        if (event == WsEvent::kStreamBegin) {
          g_processor->OnStreamBegin(session_id);

          // 发送 channel_ready 响应
          std::string ready_msg = JsonBuilder::build_envelope(
              WsEvent::kChannelReady, session_id,
              "\"latency_ms\":0,\"status\":\"receiving\"");
          g_server->Send(hdl, ready_msg);

        } else if (event == WsEvent::kStreamState) {
          std::string status = JsonBuilder::extract_data_field(payload, "status");
          g_processor->OnStreamState(session_id, status);
        }
      });

  g_server->SetDisconnectCallback(
      [](connection_hdl /*hdl*/, const std::string& session_id) {
        if (g_processor) g_processor->OnClientDisconnect(session_id);
      });

  // 4. 启动数据处理引擎
  g_processor->Start();

  // 5. 启动信令服务器 (阻塞)
  std::cout << "\n💡 提示: 在另一个终端运行客户端:"
            << "\n   cd websocket && bash scripts/run_demo.sh client"
            << "\n   或: ./build/ws-client ws://localhost:" << port
            << "\n" << std::endl;

  g_server->Run(port);

  std::cout << "✅ 服务端已退出" << std::endl;
  return 0;
}
