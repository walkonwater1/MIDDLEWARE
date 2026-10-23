/**
 * z_sub.cpp — Zenoh 原生订阅端 (完全不涉及 ROS2)
 *
 * 一个 Zenoh 订阅端最小三步:
 *
 *   1. Session::open(config)
 *   2. declare_subscriber(KeyExpr, on_sample, on_drop)
 *   3. 等回调 —— 数据到达时 Zenoh 在后台线程调用 on_sample
 *
 * Key expression 的三个层次 (本 Demo 都能试):
 *   demo/hello          — 精确匹配
 *   demo/ 后接一层通配  — 只匹配一层
 *   全局通配 (两个星号) — 匹配所有 key (会看到 zenoh 网络里的一切)
 */

// 用法:
//   ./z_sub [key] [duration_s] [connect_endpoint]
//   ./z_sub demo/hello 8
//   ./z_sub 'demo/**' 8
//   ./z_sub '**' 8 tcp/localhost:7447
//
// duration_s = 0 表示一直收 (Ctrl-C)。

#include <chrono>
#include <thread>
#include <utility>

#include "common.hxx"

int main(int argc, char ** argv)
{
  const std::string key = (argc > 1) ? argv[1] : "demo/hello";
  const double duration = (argc > 2) ? std::atof(argv[2]) : 0.0;
  const std::string connect = (argc > 3) ? argv[3] : "";

  demo::install_sigint();
  demo::banner("订阅端 (z_sub)", key, connect);

  auto session = demo::open_session(connect);

  std::atomic<long> received{0};

  // on_sample 在 Zenoh 的后台线程里被调用, 所以打印用 printf 风格的一整行,
  // 避免多个线程交叉写 std::cout 的缓冲区。
  auto subscriber = session.declare_subscriber(
    zenoh::KeyExpr(key),
    [&received](zenoh::Sample & sample) {
      const long n = ++received;
      // 一行打印三样东西: 实际命中的 key / encoding / 载荷
      std::cout << "[SUB] #" << n
                << "  key=" << sample.get_keyexpr().as_string_view()
                << "  enc=" << sample.get_encoding().as_string()
                << "  payload=" << demo::printable(sample.get_payload().as_string())
                << std::endl;
    },
    []() { std::cout << "[SUB] subscriber 已关闭" << std::endl; });

  std::cout << "[SUB] 已订阅 " << key << ", 等待数据..." << std::endl;

  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
    std::chrono::duration<double>(duration));
  while (!demo::g_stop && (duration <= 0.0 || std::chrono::steady_clock::now() < deadline)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // 退出前先 undeclare: subscriber 析构时会自动做, 这里显式写出来是为了讲清楚
  // "声明 / 撤销声明" 这个生命周期概念 (DDS 里对应 delete_datareader)。
  std::move(subscriber).undeclare();

  std::cout << "SUMMARY role=sub key=" << key << " received=" << received.load() << std::endl;
  return 0;
}
