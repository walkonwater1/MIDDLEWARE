/**
 * ws_signaling_server.h — WebSocket 信令服务器
 *
 * 基于 websocketpp, 提供:
 *   1. 多客户端会话管理 (session_id 分配与追踪)
 *   2. 事件路由与分发
 *   3. 连接/断开回调
 *   4. Ping/Pong 心跳 (协议层自动处理)
 */

#ifndef WS_SIGNALING_SERVER_H
#define WS_SIGNALING_SERVER_H

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

using websocketpp::connection_hdl;
using WsServer = websocketpp::server<websocketpp::config::asio>;

/// 单个会话的信息
struct SessionInfo {
  std::string session_id;
  std::string client_id;
  std::string channel;
  int64_t created_at_ms;
  connection_hdl hdl;

  SessionInfo() : created_at_ms(0) {}
};

/// 服务器收到的消息回调: (connection_hdl, session_id, event, payload)
using ServerMessageCallback =
    std::function<void(connection_hdl, const std::string& session_id,
                       const std::string& event, const std::string& payload)>;

/// 连接/断开回调
using ServerConnectCallback = std::function<void(connection_hdl, const std::string& session_id)>;
using ServerDisconnectCallback = std::function<void(connection_hdl, const std::string& session_id)>;

class WsSignalingServer {
 public:
  WsSignalingServer();
  ~WsSignalingServer();

  WsSignalingServer(const WsSignalingServer&) = delete;
  WsSignalingServer& operator=(const WsSignalingServer&) = delete;

  // ---- 生命周期 ----

  /// 启动服务器监听
  void Run(uint16_t port);

  /// 停止服务器
  void Stop();

  // ---- 消息发送 ----

  /// 向指定连接发送文本消息
  void Send(connection_hdl hdl, const std::string& message);

  /// 向指定 session 发送 (通过 session_id 查找连接)
  bool SendToSession(const std::string& session_id, const std::string& message);

  /// 广播给所有连接
  void Broadcast(const std::string& message);

  // ---- 会话管理 ----

  /// 为连接创建新会话, 返回 session_id
  std::string CreateSession(connection_hdl hdl, const std::string& client_id,
                             const std::string& channel);

  /// 移除会话
  void RemoveSession(connection_hdl hdl);

  /// 根据 connection_hdl 查找 session_id
  std::string GetSessionId(connection_hdl hdl) const;

  // ---- 回调注册 ----

  void SetMessageCallback(ServerMessageCallback cb) { message_cb_ = std::move(cb); }
  void SetConnectCallback(ServerConnectCallback cb) { connect_cb_ = std::move(cb); }
  void SetDisconnectCallback(ServerDisconnectCallback cb) { disconnect_cb_ = std::move(cb); }

 private:
  void on_open(connection_hdl hdl);
  void on_close(connection_hdl hdl);
  void on_message(connection_hdl hdl, WsServer::message_ptr msg);

  WsServer server_;
  std::thread server_thread_;

  // connection_hdl → SessionInfo
  std::map<connection_hdl, std::shared_ptr<SessionInfo>,
           std::owner_less<connection_hdl>>
      sessions_;
  mutable std::mutex sessions_mutex_;

  ServerMessageCallback message_cb_;
  ServerConnectCallback connect_cb_;
  ServerDisconnectCallback disconnect_cb_;
};

#endif  // WS_SIGNALING_SERVER_H
