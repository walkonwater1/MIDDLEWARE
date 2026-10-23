/**
 * common.hxx — 原生 Zenoh 示例的公共小工具 (不依赖 ROS2)
 *
 * 这里只放几个与教学无关、但每个例子都要用的小工具:
 *   open_session() / install_sigint() / banner() / printable() / json_payload()
 *
 * 刻意不封装 pub/sub/query 本身 —— 那几步是 Demo 的主角, 应该原样留在
 * 各个 .cpp 里让人直接看到。
 */

#pragma once

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

#include "zenoh.hxx"

namespace demo {

/// 全局停止标志。Zenoh 的回调在后台线程里跑, 用原子量通知主循环收工。
inline std::atomic<bool> g_stop{false};

inline void install_sigint()
{
  auto handler = [](int) { g_stop = true; };
  std::signal(SIGINT, handler);
  std::signal(SIGTERM, handler);
}

/**
 * 打开一个 Zenoh Session。
 *
 * 两种接入方式 —— 这正是 Zenoh 与 DDS 组播最大的差别:
 *
 *   1. connect 为空 (默认): peer 模式。默认配置里组播 scouting 是开的,
 *      同一台机器 / 同一网段里的其它 member 会自动被发现, 不需要路由器。
 *
 *   2. connect 非空 (如 tcp/localhost:7447):
 *      切成 client 模式, 主动连到指定端点。跨网段、穿 NAT 靠的就是这一条
 *      "我主动连出去" —— DDS 的组播做不到。
 *
 * 关掉组播 scouting 是配套动作: client 模式下发现完全由路由器负责,
 * 再开着组播只会误连到本机其它 zenoh 网络 (比如另一个 ROS_DOMAIN_ID)。
 */
inline zenoh::Session open_session(const std::string & connect_endpoint)
{
  auto config = zenoh::Config::create_default();

  if (!connect_endpoint.empty()) {
    // insert_json5 的第二个参数是**JSON 字面量**, 所以字符串要自己带引号
    config.insert_json5("mode", "\"client\"");
    config.insert_json5("connect/endpoints", "[\"" + connect_endpoint + "\"]");
    config.insert_json5("scouting/multicast/enabled", "false");
  }

  return zenoh::Session::open(std::move(config));
}

inline void banner(
  const std::string & role, const std::string & key, const std::string & connect_endpoint)
{
  std::cout << "┌─ Zenoh 原生示例 ─────────────────────────────────────" << std::endl;
  std::cout << "│ 角色          : " << role << std::endl;
  std::cout << "│ Key expression: " << key << std::endl;
  if (connect_endpoint.empty()) {
    std::cout << "│ 会话模式      : peer (默认组播 scouting, 无需路由器)" << std::endl;
  } else {
    std::cout << "│ 会话模式      : client, 主动连接 " << connect_endpoint << std::endl;
  }
  std::cout << "└──────────────────────────────────────────────────────" << std::endl;
}

/**
 * 把任意字节串变成可安全打印的一行。
 *
 * DDS/Zenoh 的载荷可以是任意二进制 (比如 ROS2 的 CDR 编码, 前 4 个字节是
 * \x00\x01\x00\x00)。直接往终端写空字节会弄乱输出, 所以这里把不可打印
 * 字节转义成 \xHH —— 既保留信息, 又不污染终端。
 */
inline std::string printable(const std::string & raw)
{
  static const char * hex = "0123456789abcdef";
  std::string out;
  out.reserve(raw.size());
  for (unsigned char c : raw) {
    if (c >= 0x20 && c < 0x7f) {
      out.push_back(static_cast<char>(c));
    } else {
      out += "\\x";
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0x0f]);
    }
  }
  return out;
}

inline std::string hostname()
{
  char buf[256] = {0};
  if (gethostname(buf, sizeof(buf) - 1) != 0) {
    return "unknown";
  }
  return std::string(buf);
}

/// 载荷统一用 JSON, 顺便演示 Encoding 是消息的一部分 (DDS 里没有这个概念)
inline std::string json_payload(
  const std::string & role, long seq, const std::string & host)
{
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();

  return std::string("{\"src\":\"") + role +
         "\",\"seq\":" + std::to_string(seq) +
         ",\"host\":\"" + host +
         "\",\"ts_ms\":" + std::to_string(now_ms) + "}";
}

}  // namespace demo
