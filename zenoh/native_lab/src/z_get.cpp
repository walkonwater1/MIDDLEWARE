/**
 * z_get.cpp — Zenoh 的"客户端" (一次性查询)
 *
 * 与 z_sub 的关键区别 (**这是最容易混的一点**):
 *
 *   z_sub : 订阅 → 之后每来一条就回调一次 (持续的数据流)
 *   z_get : 查询 → 现在问一次, 收集当前所有 Queryable 的应答, 然后结束
 *
 * 所以对一个只有 publisher、没有 queryable 的 key 做 get, 结果一定是 0 条。
 * 这不是"没连上", 而是查询模型本身如此 —— 对应 ../README.md 里
 * "为什么普通 GET 可能是空的"。
 */

// 用法:
//   ./z_get [selector] [timeout_ms] [connect_endpoint]
//   ./z_get 'demo/robot/**' 2000

#include <chrono>
#include <iostream>

#include "common.hxx"

int main(int argc, char ** argv)
{
  const std::string selector = (argc > 1) ? argv[1] : "demo/robot/**";
  const unsigned long timeout_ms = (argc > 2) ? std::strtoul(argv[2], nullptr, 10) : 2000;
  const std::string connect = (argc > 3) ? argv[3] : "";

  demo::install_sigint();
  demo::banner("查询客户端 (z_get)", selector, connect);

  auto session = demo::open_session(connect);

  zenoh::Session::GetOptions opts;
  opts.timeout_ms = timeout_ms;

  // 用 FifoChannel 收应答: get 的应答是"有限条", 收完 channel 会断开。
  // 也可以换成回调版 get(key, params, on_reply, on_drop) —— 两种写法等价。
  auto replies = session.get(
    zenoh::KeyExpr(selector), "", zenoh::channels::FifoChannel(16), std::move(opts));

  long ok = 0;
  long err = 0;

  while (true) {
    // recv() 返回 variant<Reply, RecvError>:
    //   拿到 Reply    → 继续
    //   Z_DISCONNECTED → 所有应答都收完了 (或超时)
    //   Z_NODATA       → 缓冲区暂时为空 (本 Demo 走阻塞 recv, 一般不会遇到)
    auto result = replies.recv();
    if (std::holds_alternative<zenoh::channels::RecvError>(result)) {
      break;
    }

    zenoh::Reply & reply = std::get<zenoh::Reply>(result);
    if (reply.is_ok()) {
      const zenoh::Sample & sample = reply.get_ok();
      const std::string payload = demo::printable(sample.get_payload().as_string());

      // 非 --connect 模式下, 本机自己的 queryable 也会被问到 (Zenoh 默认
      // 允许本地 loopback), 所以这里能直接看到自己刚才声明的服务。
      std::cout << "[GET] reply  key=" << sample.get_keyexpr().as_string_view()
                << "  enc=" << sample.get_encoding().as_string()
                << "  payload=" << payload << std::endl;
      ++ok;
    } else {
      std::cout << "[GET] error  " << reply.get_err().get_payload().as_string() << std::endl;
      ++err;
    }

    if (demo::g_stop) {
      break;
    }
  }

  std::cout << "SUMMARY role=get selector=" << selector << " replies=" << ok
            << " errors=" << err << std::endl;
  return 0;
}
