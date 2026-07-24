/**
 * ws_signaling_client.h — 手写 WebSocket 客户端 (RFC 6455)
 *
 * 从生产代码 (ehr_ros_app) 移植核心逻辑:
 *   - TCP 非阻塞连接 + poll 超时
 *   - HTTP Upgrade 握手
 *   - WebSocket 帧编解码 (文本帧、关闭帧、Ping/Pong)
 *   - 后台读循环 (poll + heartbeat)
 *   - 线程安全发送
 *
 * 变更:
 *   - 事件名使用通用命名 (session_init / stream_begin / ...)
 *   - 去除 ROS2 依赖 (日志 → std::cout/cerr)
 *   - 简化接口, 仅保留 Demo 所需功能
 */

#ifndef WS_SIGNALING_CLIENT_H
#define WS_SIGNALING_CLIENT_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class WsSignalingClient {
 public:
  /// 握手成功后的会话信息
  struct SessionInfo {
    std::string session_id;
    std::string data_channel;  // 模拟 rtsp_url
    std::string client_id;
    std::string channel;
  };

  /// 收到服务器推送消息的回调
  using MessageCallback = std::function<void(const std::string& message)>;

  /// 连接断开回调
  using DisconnectCallback = std::function<void()>;

  WsSignalingClient();
  ~WsSignalingClient();

  WsSignalingClient(const WsSignalingClient&) = delete;
  WsSignalingClient& operator=(const WsSignalingClient&) = delete;

  // ---- 连接 ----

  /// 同步连接: TCP → HTTP 升级 → 发送 session_init → 接收 session_ready
  /// @param ws_url    e.g. "ws://localhost:9002"
  /// @param client_id 客户端标识
  /// @param channel   业务通道
  /// @param out_info  输出会话信息
  /// @param out_error 输出错误信息
  /// @return true on success
  bool Connect(const std::string& ws_url,
               const std::string& client_id,
               const std::string& channel,
               SessionInfo& out_info,
               std::string& out_error);

  // ---- 发送 ----

  /// 发送 stream_begin (通知服务器开始推流)
  bool NotifyStreamBegin(const std::string& session_id, int64_t push_started_ms);

  /// 发送 stream_state (通知服务器流状态变更)
  /// @param status "started" (开始播放, 服务器应暂停处理) 或 "completed" (播放结束)
  bool NotifyStreamState(const std::string& session_id, const std::string& status);

  /// 发送任意文本消息 (线程安全)
  bool SendText(const std::string& message);

  // ---- 状态 ----

  bool IsConnected() const { return connected_.load(); }

  // ---- 回调 ----

  void SetMessageCallback(MessageCallback cb) { message_cb_ = std::move(cb); }
  void SetDisconnectCallback(DisconnectCallback cb) { disconnect_cb_ = std::move(cb); }

  // ---- 配置 ----

  void SetTimeoutConfig(int heartbeat_sec, int connect_timeout_ms, int io_timeout_sec);

  // ---- 断开 ----

  void Disconnect();

 private:
  // URL 解析
  static bool ParseUrl(const std::string& ws_url,
                       std::string& host, int& port, std::string& path);

  // HTTP 升级
  bool SendHttpUpgrade(const std::string& host, int port, const std::string& path);
  bool ReceiveHttpResponse();

  // WebSocket 帧
  bool SendTextFrame(const std::string& payload);
  bool ReceiveFrame(std::string& payload);

  static std::string GenerateMaskingKey();
  static std::string GenerateWebSocketKey();
  static std::string BuildFrame(const std::string& payload);

  // 后台读
  void ReadLoop();

  // 心跳
  void SendHeartbeat();

  // 底层 I/O
  bool ReadExact(void* buf, size_t n);
  bool WriteAll(const void* buf, size_t n);

  int sock_;
  std::atomic<bool> connected_;
  std::atomic<bool> need_exit_;
  std::timed_mutex send_mutex_;
  std::thread read_thread_;
  MessageCallback message_cb_;
  DisconnectCallback disconnect_cb_;

  int heartbeat_sec_ = 15;
  int connect_timeout_ms_ = 5000;
  int io_timeout_sec_ = 5;
};

#endif  // WS_SIGNALING_CLIENT_H
