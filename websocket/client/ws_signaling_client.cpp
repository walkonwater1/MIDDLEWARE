/**
 * ws_signaling_client.cpp — 手写 WebSocket 客户端实现 (RFC 6455)
 *
 * 从 ehr_ros_app websocket_rtsp_client 移植, 去除 ROS2 依赖, 事件名通用化。
 */

#include "ws_signaling_client.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <sstream>
#include <vector>

#include "common/ws_common.h"

// ===========================================================================
// 超时配置
// ===========================================================================

void WsSignalingClient::SetTimeoutConfig(int heartbeat_sec,
                                          int connect_timeout_ms,
                                          int io_timeout_sec) {
  heartbeat_sec_ = heartbeat_sec;
  connect_timeout_ms_ = connect_timeout_ms;
  io_timeout_sec_ = io_timeout_sec;
  std::cout << "[WS] 超时配置: heartbeat=" << heartbeat_sec_
            << "s, connect=" << connect_timeout_ms_
            << "ms, io=" << io_timeout_sec_ << "s" << std::endl;
}

// ===========================================================================
// 构造/析构
// ===========================================================================

WsSignalingClient::WsSignalingClient()
    : sock_(-1), connected_(false), need_exit_(false) {}

WsSignalingClient::~WsSignalingClient() { Disconnect(); }

// ===========================================================================
// 连接
// ===========================================================================

bool WsSignalingClient::Connect(const std::string& ws_url,
                                 const std::string& client_id,
                                 const std::string& channel,
                                 SessionInfo& out_info,
                                 std::string& out_error) {
  // 1. 解析 URL
  std::string host, path;
  int port = 0;
  if (!ParseUrl(ws_url, host, port, path)) {
    out_error = "URL 解析失败: " + ws_url;
    std::cerr << "[WS] " << out_error << std::endl;
    return false;
  }
  std::cout << "[WS] 目标: host=" << host << ", port=" << port
            << ", path=" << path << std::endl;

  // 2. TCP 连接 (非阻塞 + poll 超时)
  sock_ = socket(AF_INET, SOCK_STREAM, 0);
  if (sock_ < 0) {
    out_error = "socket() 失败: " + std::string(strerror(errno));
    std::cerr << "[WS] " << out_error << std::endl;
    return false;
  }

  // TCP_NODELAY
  {
    int opt = 1;
    setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
  }

  // DNS
  struct hostent* server = gethostbyname(host.c_str());
  if (!server) {
    out_error = "DNS 解析失败: " + host;
    std::cerr << "[WS] " << out_error << std::endl;
    close(sock_); sock_ = -1;
    return false;
  }

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
  addr.sin_port = htons(port);

  // 非阻塞 connect
  {
    int flags = fcntl(sock_, F_GETFL, 0);
    fcntl(sock_, F_SETFL, flags | O_NONBLOCK);
  }

  std::cout << "[WS] 连接中 " << host << ":" << port << "..." << std::endl;
  int ret = connect(sock_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
  if (ret < 0 && errno != EINPROGRESS) {
    out_error = "connect() 失败: " + std::string(strerror(errno));
    std::cerr << "[WS] " << out_error << std::endl;
    close(sock_); sock_ = -1;
    return false;
  }

  if (ret != 0) {
    // EINPROGRESS — 等待完成
    struct pollfd pfd;
    pfd.fd = sock_;
    pfd.events = POLLOUT;
    int poll_ret = poll(&pfd, 1, connect_timeout_ms_);
    if (poll_ret <= 0) {
      out_error = "connect() 超时";
      std::cerr << "[WS] " << out_error << std::endl;
      close(sock_); sock_ = -1;
      return false;
    }
    int so_error = 0;
    socklen_t len = sizeof(so_error);
    getsockopt(sock_, SOL_SOCKET, SO_ERROR, &so_error, &len);
    if (so_error != 0) {
      out_error = "connect() 失败: " + std::string(strerror(so_error));
      std::cerr << "[WS] " << out_error << std::endl;
      close(sock_); sock_ = -1;
      return false;
    }
  }
  std::cout << "[WS] TCP 已连接" << std::endl;

  // 恢复阻塞模式
  {
    int flags = fcntl(sock_, F_GETFL, 0);
    fcntl(sock_, F_SETFL, flags & ~O_NONBLOCK);
  }

  // 设置 I/O 超时
  {
    struct timeval tv;
    tv.tv_sec = io_timeout_sec_;
    tv.tv_usec = 0;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  }

  // 3. HTTP 升级
  if (!SendHttpUpgrade(host, port, path)) {
    out_error = "HTTP 升级请求失败";
    std::cerr << "[WS] " << out_error << std::endl;
    close(sock_); sock_ = -1;
    return false;
  }
  if (!ReceiveHttpResponse()) {
    out_error = "HTTP 升级响应失败 (期望 101)";
    std::cerr << "[WS] " << out_error << std::endl;
    close(sock_); sock_ = -1;
    return false;
  }
  std::cout << "[WS] HTTP 升级完成 (101)" << std::endl;

  // 清除接收超时 (后台读循环使用 poll 管理超时)
  {
    struct timeval tv{};
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  }

  connected_.store(true);

  // 4. 发送 session_init
  {
    std::string data_json = "\"client_id\":\"" + client_id +
                            "\",\"channel\":\"" + channel + "\"";
    std::string msg =
        JsonBuilder::build_envelope(WsEvent::kSessionInit, "", data_json);
    std::cout << "[WS] 发送 session_init: " << msg << std::endl;
    if (!SendTextFrame(msg)) {
      out_error = "发送 session_init 失败";
      connected_.store(false);
      close(sock_); sock_ = -1;
      return false;
    }
  }

  // 5. 接收 session_ready
  {
    std::string response;
    if (!ReceiveFrame(response)) {
      out_error = "接收 session_ready 失败";
      connected_.store(false);
      close(sock_); sock_ = -1;
      return false;
    }
    std::cout << "[WS] 收到: " << response << std::endl;

    out_info.session_id = JsonBuilder::extract_session_id(response);
    out_info.data_channel = JsonBuilder::extract_data_field(response, "data_channel");
    out_info.client_id = JsonBuilder::extract_data_field(response, "client_id");
    out_info.channel = JsonBuilder::extract_data_field(response, "channel");

    if (out_info.session_id.empty()) {
      out_error = "响应中缺少 session_id";
      connected_.store(false);
      close(sock_); sock_ = -1;
      return false;
    }

    std::cout << "[WS] 会话建立: session_id=" << out_info.session_id
              << ", data_channel=" << out_info.data_channel << std::endl;
  }

  // 6. 启动后台读循环
  need_exit_.store(false);
  read_thread_ = std::thread(&WsSignalingClient::ReadLoop, this);
  std::cout << "[WS] 后台读循环已启动" << std::endl;

  return true;
}

// ===========================================================================
// 发送消息
// ===========================================================================

bool WsSignalingClient::NotifyStreamBegin(const std::string& session_id,
                                           int64_t push_started_ms) {
  if (!connected_.load() || sock_ < 0) return false;

  std::string data_json = "\"push_time\":" + std::to_string(push_started_ms);
  std::string msg = JsonBuilder::build_envelope(
      WsEvent::kStreamBegin, session_id, data_json);
  std::cout << "[WS] 发送 stream_begin: " << msg << std::endl;

  std::unique_lock<std::timed_mutex> lock(send_mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(3000))) {
    std::cerr << "[WS] 发送 stream_begin: 锁繁忙, 丢弃" << std::endl;
    return false;
  }
  return SendTextFrame(msg);
}

bool WsSignalingClient::NotifyStreamState(const std::string& session_id,
                                           const std::string& status) {
  if (!connected_.load() || sock_ < 0) return false;

  std::string data_json = "\"status\":\"" + status + "\"";
  std::string msg = JsonBuilder::build_envelope(
      WsEvent::kStreamState, session_id, data_json);
  std::cout << "[WS] 发送 stream_state(" << status << "): " << msg << std::endl;

  std::unique_lock<std::timed_mutex> lock(send_mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(2000))) {
    std::cerr << "[WS] 发送 stream_state: 锁繁忙, 丢弃" << std::endl;
    return false;
  }
  return SendTextFrame(msg);
}

bool WsSignalingClient::SendText(const std::string& message) {
  if (!connected_.load() || sock_ < 0) return false;
  std::unique_lock<std::timed_mutex> lock(send_mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(2000))) return false;
  return SendTextFrame(message);
}

// ===========================================================================
// 断开
// ===========================================================================

void WsSignalingClient::Disconnect() {
  if (!connected_.load() && sock_ < 0) return;
  std::cout << "[WS] 断开连接..." << std::endl;

  connected_.store(false);
  need_exit_.store(true);

  // 发送关闭帧
  if (sock_ >= 0) {
    uint16_t status_code = htons(1000);
    std::string close_payload(reinterpret_cast<const char*>(&status_code), 2);
    {
      std::unique_lock<std::timed_mutex> lock(send_mutex_, std::defer_lock);
      if (lock.try_lock_for(std::chrono::milliseconds(2000))) {
        std::string frame;
        uint8_t b0 = 0x80 | 0x08;  // FIN + Close
        std::string mask_key = GenerateMaskingKey();
        std::string masked = close_payload;
        for (size_t i = 0; i < masked.size(); i++)
          masked[i] ^= mask_key[i % 4];
        size_t len = masked.size();
        frame.push_back(static_cast<char>(b0));
        frame.push_back(static_cast<char>(0x80 | static_cast<uint8_t>(len)));
        frame += mask_key;
        frame += masked;
        WriteAll(frame.data(), frame.size());
      }
    }
  }

  // 关闭 socket 唤醒读线程
  if (sock_ >= 0) {
    shutdown(sock_, SHUT_RDWR);
  }
  if (read_thread_.joinable()) {
    read_thread_.join();
  }
  if (sock_ >= 0) {
    close(sock_);
    sock_ = -1;
  }
  std::cout << "[WS] 已断开" << std::endl;
}

// ===========================================================================
// URL 解析
// ===========================================================================

bool WsSignalingClient::ParseUrl(const std::string& ws_url,
                                  std::string& host, int& port,
                                  std::string& path) {
  const std::string kPrefix = "ws://";
  if (ws_url.compare(0, kPrefix.size(), kPrefix) != 0) {
    std::cerr << "[WS] URL 必须以 ws:// 开头: " << ws_url << std::endl;
    return false;
  }
  std::string remaining = ws_url.substr(kPrefix.size());
  size_t path_start = remaining.find('/');
  std::string host_port;
  if (path_start == std::string::npos) {
    host_port = remaining;
    path = "/";
  } else {
    host_port = remaining.substr(0, path_start);
    path = remaining.substr(path_start);
  }
  size_t colon_pos = host_port.find(':');
  if (colon_pos != std::string::npos) {
    host = host_port.substr(0, colon_pos);
    try {
      port = std::stoi(host_port.substr(colon_pos + 1));
    } catch (...) {
      std::cerr << "[WS] 端口解析失败: " << ws_url << std::endl;
      return false;
    }
  } else {
    host = host_port;
    port = 80;
  }
  return true;
}

// ===========================================================================
// HTTP 升级
// ===========================================================================

bool WsSignalingClient::SendHttpUpgrade(const std::string& host,
                                         int port, const std::string& path) {
  std::string ws_key = GenerateWebSocketKey();
  std::ostringstream req;
  req << "GET " << path << " HTTP/1.1\r\n"
      << "Host: " << host;
  if (port != 80) req << ":" << port;
  req << "\r\n"
      << "Upgrade: websocket\r\n"
      << "Connection: Upgrade\r\n"
      << "Sec-WebSocket-Key: " << ws_key << "\r\n"
      << "Sec-WebSocket-Version: 13\r\n"
      << "\r\n";
  std::string req_str = req.str();
  std::cout << "[WS] HTTP 升级请求:\n" << req_str;
  return WriteAll(req_str.data(), req_str.size());
}

bool WsSignalingClient::ReceiveHttpResponse() {
  std::string response;
  char buf[4096];
  while (true) {
    ssize_t n = recv(sock_, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
      std::cerr << "[WS] HTTP 握手 recv 失败: "
                << (n == 0 ? "连接关闭" : strerror(errno)) << std::endl;
      return false;
    }
    buf[n] = '\0';
    response += buf;
    size_t end_pos = response.find("\r\n\r\n");
    if (end_pos != std::string::npos) {
      std::string headers = response.substr(0, end_pos + 4);
      std::cout << "[WS] HTTP 响应:\n" << headers;
      if (headers.find("101") == std::string::npos) {
        std::cerr << "[WS] 期望 HTTP 101" << std::endl;
        return false;
      }
      return true;
    }
    if (response.size() > 16384) {
      std::cerr << "[WS] HTTP 响应头过大" << std::endl;
      return false;
    }
  }
}

// ===========================================================================
// WebSocket 密钥生成
// ===========================================================================

std::string WsSignalingClient::GenerateWebSocketKey() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 255);
  unsigned char key[16];
  for (int i = 0; i < 16; i++) key[i] = static_cast<unsigned char>(dis(gen));
  // Base64 编码
  static const char kBase64Chars[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(24);
  for (int i = 0; i < 16; i += 3) {
    unsigned int n = (key[i] << 16) | (key[i+1] << 8) | key[i+2];
    out += kBase64Chars[(n >> 18) & 0x3F];
    out += kBase64Chars[(n >> 12) & 0x3F];
    out += kBase64Chars[(n >> 6) & 0x3F];
    out += kBase64Chars[n & 0x3F];
  }
  out[out.size() - 1] = '=';
  out[out.size() - 2] = '=';
  return out;
}

std::string WsSignalingClient::GenerateMaskingKey() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 255);
  char key[4];
  for (int i = 0; i < 4; i++) key[i] = static_cast<char>(dis(gen));
  return std::string(key, 4);
}

// ===========================================================================
// WebSocket 帧构建
// ===========================================================================

std::string WsSignalingClient::BuildFrame(const std::string& payload) {
  std::string frame;
  frame.push_back(static_cast<char>(0x81));  // FIN + text opcode
  size_t len = payload.size();
  if (len < 126) {
    frame.push_back(static_cast<char>(0x80 | static_cast<uint8_t>(len)));
  } else if (len <= 65535) {
    frame.push_back(static_cast<char>(0x80 | 126));
    frame.push_back(static_cast<char>((len >> 8) & 0xFF));
    frame.push_back(static_cast<char>(len & 0xFF));
  } else {
    frame.push_back(static_cast<char>(0x80 | 127));
    for (int i = 7; i >= 0; i--)
      frame.push_back(static_cast<char>((len >> (i * 8)) & 0xFF));
  }
  std::string mask_key = GenerateMaskingKey();
  frame += mask_key;
  for (size_t i = 0; i < payload.size(); i++)
    frame.push_back(payload[i] ^ mask_key[i % 4]);
  return frame;
}

// ===========================================================================
// 收发帧
// ===========================================================================

bool WsSignalingClient::SendTextFrame(const std::string& payload) {
  std::string frame = BuildFrame(payload);
  return WriteAll(frame.data(), frame.size());
}

bool WsSignalingClient::ReceiveFrame(std::string& payload) {
  payload.clear();
  uint8_t header[2];
  if (!ReadExact(header, 2)) return false;
  uint8_t opcode = header[0] & 0x0F;
  bool masked = (header[1] & 0x80) != 0;
  uint64_t payload_len = header[1] & 0x7F;

  if (payload_len == 126) {
    uint8_t ext[2];
    if (!ReadExact(ext, 2)) return false;
    payload_len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
  } else if (payload_len == 127) {
    uint8_t ext[8];
    if (!ReadExact(ext, 8)) return false;
    payload_len = 0;
    for (int i = 0; i < 8; i++)
      payload_len = (payload_len << 8) | ext[i];
  }

  uint8_t mask_key[4] = {0};
  if (masked) {
    if (!ReadExact(mask_key, 4)) return false;
  }

  if (payload_len > 0) {
    std::vector<uint8_t> buf(payload_len);
    if (!ReadExact(buf.data(), payload_len)) return false;
    if (masked) {
      for (size_t i = 0; i < payload_len; i++)
        buf[i] ^= mask_key[i % 4];
    }
    payload.assign(reinterpret_cast<const char*>(buf.data()), payload_len);
  }

  // 控制帧处理
  if (opcode == 0x08) {
    // Close
    std::cout << "[WS] 收到关闭帧" << std::endl;
    connected_.store(false);
    return false;
  } else if (opcode == 0x09) {
    // Ping → Pong
    std::cout << "[WS] 收到 Ping, 回复 Pong" << std::endl;
    std::unique_lock<std::timed_mutex> lock(send_mutex_, std::defer_lock);
    if (lock.try_lock_for(std::chrono::milliseconds(1000))) {
      std::string pong_frame;
      pong_frame.push_back(static_cast<char>(0x8A));  // FIN + Pong
      size_t len = payload.size();
      if (len < 126) {
        pong_frame.push_back(static_cast<char>(0x80 | static_cast<uint8_t>(len)));
      } else if (len <= 65535) {
        pong_frame.push_back(static_cast<char>(0x80 | 126));
        pong_frame.push_back(static_cast<char>((len >> 8) & 0xFF));
        pong_frame.push_back(static_cast<char>(len & 0xFF));
      } else {
        pong_frame.push_back(static_cast<char>(0x80 | 127));
        for (int i = 7; i >= 0; i--)
          pong_frame.push_back(static_cast<char>((len >> (i * 8)) & 0xFF));
      }
      std::string mk = GenerateMaskingKey();
      pong_frame += mk;
      for (size_t i = 0; i < payload.size(); i++)
        pong_frame.push_back(payload[i] ^ mk[i % 4]);
      WriteAll(pong_frame.data(), pong_frame.size());
    }
    return ReceiveFrame(payload);  // 递归等待下一个数据帧
  } else if (opcode == 0x0A) {
    // Pong — 忽略
    std::cout << "[WS] 收到 Pong" << std::endl;
    return ReceiveFrame(payload);
  }
  return true;
}

// ===========================================================================
// 后台读循环
// ===========================================================================

void WsSignalingClient::ReadLoop() {
  std::cout << "[WS] 读循环启动" << std::endl;
  auto last_heartbeat = std::chrono::steady_clock::now();

  while (!need_exit_.load() && connected_.load()) {
    struct pollfd pfd;
    pfd.fd = sock_;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, 1000);

    if (ret < 0) {
      std::cerr << "[WS] poll 错误: " << strerror(errno) << std::endl;
      connected_.store(false);
      break;
    }

    // 心跳
    auto now = std::chrono::steady_clock::now();
    if (now - last_heartbeat >= std::chrono::seconds(heartbeat_sec_)) {
      SendHeartbeat();
      last_heartbeat = now;
    }

    if (ret == 0) continue;  // 超时

    std::string message;
    if (!ReceiveFrame(message)) {
      if (connected_.load()) {
        std::cerr << "[WS] 连接丢失, 读循环退出" << std::endl;
        connected_.store(false);
      }
      break;
    }

    last_heartbeat = std::chrono::steady_clock::now();

    if (!message.empty() && message_cb_) {
      std::cout << "[WS] 服务器消息: " << message << std::endl;
      message_cb_(message);
    }
  }

  std::cout << "[WS] 读循环退出" << std::endl;
  if (disconnect_cb_) disconnect_cb_();
}

// ===========================================================================
// 心跳
// ===========================================================================

void WsSignalingClient::SendHeartbeat() {
  if (!connected_.load() || sock_ < 0) return;
  std::string msg = JsonBuilder::build_envelope(WsEvent::kHeartbeat, "", "");
  std::cout << "[WS] 发送 heartbeat" << std::endl;

  std::unique_lock<std::timed_mutex> lock(send_mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(1000))) return;
  if (!connected_.load() || sock_ < 0) return;
  SendTextFrame(msg);
}

// ===========================================================================
// 底层 I/O
// ===========================================================================

bool WsSignalingClient::ReadExact(void* buf, size_t n) {
  size_t total = 0;
  auto* p = static_cast<uint8_t*>(buf);
  while (total < n) {
    ssize_t r = recv(sock_, p + total, n - total, 0);
    if (r <= 0) return false;
    total += static_cast<size_t>(r);
  }
  return true;
}

bool WsSignalingClient::WriteAll(const void* buf, size_t n) {
  size_t total = 0;
  auto* p = static_cast<const uint8_t*>(buf);
  while (total < n) {
    ssize_t w = send(sock_, p + total, n - total, MSG_NOSIGNAL);
    if (w <= 0) return false;
    total += static_cast<size_t>(w);
  }
  return true;
}
