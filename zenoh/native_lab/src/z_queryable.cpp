/**
 * z_queryable.cpp — Zenoh 的"服务端" (Queryable)
 *
 * Zenoh 的请求-响应模型叫 Query/Queryable —— 它不是 DDS 的 Service 换皮,
 * 差别值得理解:
 *
 *   - 查询方用 key expression **检索** (可以是通配符, 一次问一片)
 *   - 每个匹配的 Queryable 各自回一个 reply (0 个、1 个、N 个都正常)
 *   - 只有一个查询方问完才算结束 (query 的 drop 回调)
 *   - 普通 GET 只命中 Queryable, **不会**读到 pub/sub 的历史数据
 *
 * 最后这一点正是 ../scripts/rest_probe.sh 里"普通 GET 可能返回空"的原因。
 *
 * 程序本身三步:
 *   1. Session::open(config)
 *   2. declare_queryable(KeyExpr, on_query, on_drop)
 *   3. 在 on_query 里 query.reply(...)
 */

// 用法:
//   ./z_queryable [key] [duration_s] [connect_endpoint]
//   ./z_queryable 'demo/robot/*' 20

#include <chrono>
#include <thread>

#include "common.hxx"

int main(int argc, char ** argv)
{
  const std::string key = (argc > 1) ? argv[1] : "demo/robot/**";
  const double duration = (argc > 2) ? std::atof(argv[2]) : 0.0;
  const std::string connect = (argc > 3) ? argv[3] : "";

  demo::install_sigint();
  demo::banner("查询服务端 (z_queryable)", key, connect);

  auto session = demo::open_session(connect);

  std::atomic<long> answered{0};

  auto queryable = session.declare_queryable(
    zenoh::KeyExpr(key),
    [&answered](zenoh::Query & query) {
      // 查询可以带参数 (在 key 后面用 ?k=v 写), 这里原样回显
      std::string parameters;
      if (query.get_payload().has_value()) {
        parameters = query.get_payload()->get().as_string();
      }

      const std::string reply =
        std::string("{") +
        "\"service\":\"z_queryable\"," +
        "\"key\":\"" + std::string(query.get_keyexpr().as_string_view()) + "\"," +
        "\"query_params\":\"" + parameters + "\"," +
        "\"host\":\"" + demo::hostname() + "\"}";

      zenoh::Query::ReplyOptions opts;
      opts.encoding = zenoh::Encoding::Predefined::application_json();

      // 注意: reply 的 key 由 queryable 自己决定 —— 可以用查询的 key,
      // 也可以回一个更具体的子 key (如 demo/robot/1/status)。
      query.reply(query.get_keyexpr(), zenoh::Bytes(reply), std::move(opts));

      const long n = ++answered;
      std::cout << "[QUERYABLE] #" << n << "  <- query key="
                << query.get_keyexpr().as_string_view()
                << "  params=" << (parameters.empty() ? "(none)" : parameters)
                << "  -> reply=" << reply << std::endl;
    },
    []() { std::cout << "[QUERYABLE] queryable 已关闭" << std::endl; });

  std::cout << "[QUERYABLE] 已声明 " << key << ", 等待 GET..." << std::endl;

  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
    std::chrono::duration<double>(duration));
  while (!demo::g_stop && (duration <= 0.0 || std::chrono::steady_clock::now() < deadline)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::move(queryable).undeclare();
  std::cout << "SUMMARY role=queryable key=" << key << " answered=" << answered.load()
            << std::endl;
  return 0;
}
