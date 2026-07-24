/**
 * ws_signaling_server.cpp — WebSocket 信令服务器实现
 */

#include "ws_signaling_server.h"

#include <iostream>

#include "common/ws_common.h"

WsSignalingServer::WsSignalingServer() {
  server_.init_asio();
  server_.set_reuse_addr(true);

  server_.set_open_handler(
      [this](connection_hdl hdl) { on_open(hdl); });
  server_.set_close_handler(
      [this](connection_hdl hdl) { on_close(hdl); });
  server_.set_message_handler(
      [this](connection_hdl hdl, WsServer::message_ptr msg) {
        on_message(hdl, msg);
      });
}

WsSignalingServer::~WsSignalingServer() { Stop(); }

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------

void WsSignalingServer::Run(uint16_t port) {
  server_.listen(port);
  server_.start_accept();

  std::cout << "\n========================================" << std::endl;
  std::cout << "  WebSocket 信令服务" << std::endl;
  std::cout << "  监听: ws://0.0.0.0:" << port << std::endl;
  std::cout << "  特性:" << std::endl;
  std::cout << "    • 多客户端会话管理" << std::endl;
  std::cout << "    • 事件路由与分发" << std::endl;
  std::cout << "    • 自动 Ping/Pong 心跳" << std::endl;
  std::cout << "========================================\n" << std::endl;

  server_.run();
}

void WsSignalingServer::Stop() {
  try {
    server_.stop_listening();

    // 关闭所有连接
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& [hdl, info] : sessions_) {
      try {
        server_.close(hdl, websocketpp::close::status::going_away,
                       "server shutdown");
      } catch (...) {}
    }
  } catch (...) {}

  // websocketpp 的 run() 会在 stop_listening + 所有连接关闭后返回
}

// ---------------------------------------------------------------------------
// 消息发送
// ---------------------------------------------------------------------------

void WsSignalingServer::Send(connection_hdl hdl, const std::string& message) {
  try {
    server_.send(hdl, message, websocketpp::frame::opcode::text);
  } catch (const std::exception& e) {
    std::cerr << "[SERVER] 发送失败: " << e.what() << std::endl;
  }
}

bool WsSignalingServer::SendToSession(const std::string& session_id,
                                       const std::string& message) {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  for (auto& [hdl, info] : sessions_) {
    if (info->session_id == session_id) {
      Send(hdl, message);
      return true;
    }
  }
  return false;
}

void WsSignalingServer::Broadcast(const std::string& message) {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  for (auto& [hdl, info] : sessions_) {
    try {
      server_.send(hdl, message, websocketpp::frame::opcode::text);
    } catch (...) {}
  }
}

// ---------------------------------------------------------------------------
// 会话管理
// ---------------------------------------------------------------------------

std::string WsSignalingServer::CreateSession(connection_hdl hdl,
                                              const std::string& client_id,
                                              const std::string& channel) {
  std::string sid = JsonBuilder::generate_session_id();

  auto info = std::make_shared<SessionInfo>();
  info->session_id = sid;
  info->client_id = client_id;
  info->channel = channel;
  info->created_at_ms = JsonBuilder::now_ms();
  info->hdl = hdl;

  std::lock_guard<std::mutex> lock(sessions_mutex_);
  sessions_[hdl] = info;

  std::cout << "[SERVER] 会话创建: session_id=" << sid
            << ", client_id=" << client_id
            << ", channel=" << channel
            << " (共 " << sessions_.size() << " 个会话)" << std::endl;
  return sid;
}

void WsSignalingServer::RemoveSession(connection_hdl hdl) {
  std::string sid;
  {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(hdl);
    if (it != sessions_.end()) {
      sid = it->second->session_id;
      sessions_.erase(it);
    }
  }
  if (!sid.empty()) {
    std::cout << "[SERVER] 会话移除: session_id=" << sid
              << " (剩余 " << sessions_.size() << " 个会话)" << std::endl;
  }
}

std::string WsSignalingServer::GetSessionId(connection_hdl hdl) const {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  auto it = sessions_.find(hdl);
  if (it != sessions_.end()) {
    return it->second->session_id;
  }
  return "";
}

// ---------------------------------------------------------------------------
// 回调
// ---------------------------------------------------------------------------

void WsSignalingServer::on_open(connection_hdl hdl) {
  auto conn = server_.get_con_from_hdl(hdl);
  std::cout << "[SERVER] 连接: " << conn->get_remote_endpoint() << std::endl;
}

void WsSignalingServer::on_close(connection_hdl hdl) {
  auto conn = server_.get_con_from_hdl(hdl);
  std::string sid = GetSessionId(hdl);
  std::cout << "[SERVER] 断开: " << conn->get_remote_endpoint()
            << ", session_id=" << sid << std::endl;

  if (disconnect_cb_) disconnect_cb_(hdl, sid);
  RemoveSession(hdl);
}

void WsSignalingServer::on_message(connection_hdl hdl,
                                    WsServer::message_ptr msg) {
  std::string payload = msg->get_payload();
  std::string event = JsonBuilder::extract_event(payload);
  std::string sid = JsonBuilder::extract_session_id(payload);

  auto conn = server_.get_con_from_hdl(hdl);
  std::cout << "[SERVER] 消息 ← " << conn->get_remote_endpoint()
            << " | event=" << event
            << " | payload=" << payload << std::endl;

  // 处理 session_init: 创建新会话
  if (event == WsEvent::kSessionInit) {
    std::string client_id = JsonBuilder::extract_data_field(payload, "client_id");
    std::string channel = JsonBuilder::extract_data_field(payload, "channel");
    if (client_id.empty()) client_id = "unknown";
    if (channel.empty()) channel = WsConfig::kDefaultChannel;

    std::string new_sid = CreateSession(hdl, client_id, channel);

    // 响应 session_ready 带 data_channel (模拟 rtsp_url)
    std::string data_channel = "data://localhost:" +
                               std::to_string(WsConfig::kDefaultPort) +
                               "/channel/" + new_sid;
    std::string data_json = "{\"data_channel\":\"" + data_channel +
                            "\",\"client_id\":\"" + client_id +
                            "\",\"channel\":\"" + channel + "\"}";
    std::string response =
        JsonBuilder::build_envelope_data(WsEvent::kSessionReady, new_sid, data_json);
    Send(hdl, response);
    std::cout << "[SERVER] session_ready → " << new_sid << std::endl;

    if (connect_cb_) connect_cb_(hdl, new_sid);
  }

  // 处理 heartbeat
  if (event == WsEvent::kHeartbeat) {
    // 仅记录心跳, 不回复
    std::cout << "[SERVER] 心跳 ← " << sid << std::endl;
  }

  // 所有消息转发给业务回调
  if (message_cb_) {
    message_cb_(hdl, sid, event, payload);
  }
}
