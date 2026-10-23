/**
 * z_pub.cpp — Zenoh 原生发布端 (完全不涉及 ROS2)
 *
 * 一个 Zenoh 程序最小只有四步:
 *
 *   1. Config::create_default()        — 拿一份默认配置
 *   2. Session::open(std::move(cfg))   — 接入 zenoh 网络
 *   3. declare_publisher(KeyExpr)      — 声明要发的 key expression
 *   4. publisher.put(Bytes, options)   — 发一条
 *
 * 对照 DDS: 这里没有 topic 类型、没有 QoS 协商、没有 domain participant ——
 * key 是任意层级字符串, 载荷是字节串, encoding 随消息一起走。
 *
 * 用法:
 *   ./z_pub [key] [rate_hz] [count] [connect_endpoint]
 *   ./z_pub demo/hello 5 20                    # peer 模式, 发 20 条
 *   ./z_pub demo/hello 5 0                     # peer 模式, 一直发 (Ctrl-C)
 *   ./z_pub demo/hello 5 20 tcp/localhost:7447 # client 模式, 连路由器
 *
 * count = 0 表示不限条数。
 */

#include <chrono>
#include <thread>

#include "common.hxx"

int main(int argc, char ** argv)
{
  const std::string key = (argc > 1) ? argv[1] : "demo/hello";
  const double rate_hz = (argc > 2) ? std::atof(argv[2]) : 2.0;
  const long count = (argc > 3) ? std::atol(argv[3]) : 0;
  const std::string connect = (argc > 4) ? argv[4] : "";

  demo::install_sigint();
  demo::banner("发布端 (z_pub)", key, connect);

  // ── 1/2. 配置 + 打开会话 ──────────────────────────────────
  auto session = demo::open_session(connect);

  // ── 3. 声明发布者 ─────────────────────────────────────────
  // KeyExpr 支持通配符 (demo/*), 但发布端必须用**具体 key**, 不能用通配符。
  auto publisher = session.declare_publisher(zenoh::KeyExpr(key));
  std::cout << "[PUB] publisher 已声明, 开始发布 (" << rate_hz << " Hz, count="
            << (count == 0 ? std::string("∞") : std::to_string(count)) << ")"
            << std::endl;

  // ── 4. 循环 put ───────────────────────────────────────────
  const auto period = std::chrono::duration<double>(1.0 / (rate_hz > 0 ? rate_hz : 2.0));
  auto next = std::chrono::steady_clock::now();
  long seq = 0;

  while (!demo::g_stop && (count == 0 || seq < count)) {
    const std::string payload = demo::json_payload("z_pub", seq, demo::hostname());

    zenoh::Publisher::PutOptions opts;
    opts.encoding = zenoh::Encoding::Predefined::application_json();

    publisher.put(zenoh::Bytes(payload), std::move(opts));

    std::cout << "[PUB] #" << seq << "  " << key << "  <- " << payload << std::endl;
    ++seq;

    next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
    std::this_thread::sleep_until(next);
  }

  std::cout << "SUMMARY role=pub key=" << key << " sent=" << seq << std::endl;
  return 0;
}
