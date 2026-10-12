/**
 * rmw_probe_node.cpp — RMW 无关的传输自省节点 (DDS ⇄ Zenoh 切换实验)
 *
 * 演示要点:
 *   1. 同一份代码, 三种底层传输 — rmw_fastrtps_cpp / rmw_cyclonedds_cpp / rmw_zenoh_cpp
 *   2. 节点启动即自报"我在哪条传输上": RMW 实现 id + 相关环境变量
 *   3. 消息载荷里带上发送方的 RMW id, 订阅端据此判断对端走的是哪条栈
 *   4. 退出时打印 SUMMARY 摘要行, 供脚本做断言 (例如"跨域没桥接时应收到 0 条")
 *
 * 用法:
 *   # 默认 DDS (FastDDS)
 *   ros2 run ros2_zenoh_lab rmw_probe_node --ros-args -p mode:=pub
 *
 *   # 切到 Zenoh (需先起路由器: ros2 run rmw_zenoh_cpp rmw_zenohd)
 *   RMW_IMPLEMENTATION=rmw_zenoh_cpp ros2 run ros2_zenoh_lab rmw_probe_node \
 *     --ros-args -p mode:=sub
 *
 *   # 自定义话题 / 频率 / 运行时长 / 标签
 *   ros2 run ros2_zenoh_lab rmw_probe_node --ros-args \
 *     -p mode:=both -p topic:=/fleet/telemetry -p rate_hz:=5.0 \
 *     -p label:=robot_telemetry -p exit_after:=8.0
 *
 * 参数:
 *   mode       pub | sub | both           (环境变量 ZENOH_LAB_MODE      兜底)
 *   topic      话题名, 自动补前导 '/'      (环境变量 ZENOH_LAB_TOPIC     兜底)
 *   rate_hz    发布频率 Hz, 默认 2.0       (环境变量 ZENOH_LAB_RATE_HZ   兜底)
 *   label      横幅/载荷里的角色标签       (环境变量 ZENOH_LAB_LABEL     兜底)
 *   exit_after 运行 N 秒后自动退出并打印 SUMMARY, 0 = 不限 (默认 0)
 *
 * 为什么参数要有环境变量兜底:
 *   参数的注入 (`-p`) 在 ROS2 里是走 topic/service 的, 而 rmw_zenoh_cpp 在 Humble
 *   上有"高负载时 service 调用被静默丢弃"的已知问题 (ros2/rmw_zenoh#978)。
 *   脚本在 zenoh 场景下会同时设置 -p 和环境变量, 让参数丢失不至于让 Demo 失败。
 *
 * 与 ros2/dds_vendor_lab 的关系:
 *   那个 Lab 只在 DDS 供应商之间切换 (FastDDS ⇄ CycloneDDS), 因为两者都用 RTPS
 *   线协议, 组播发现能互通。本节点多做的事是: 把"非 DDS 传输"也纳入同一次对比,
 *   并把"发现方式" (DDS 组播 SPDP/SEDP vs Zenoh router gossip) 显式打出来。
 */

#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "rmw/rmw.h"

using namespace std::chrono_literals;

namespace {

/// 读环境变量, 空/未设置时回落到 fallback
std::string env_or(const char * name, const std::string & fallback)
{
  const char * v = std::getenv(name);
  return (v != nullptr && *v != '\0') ? std::string(v) : fallback;
}

/// 读环境变量, 未设置时返回可读的占位串 (用于横幅展示)
std::string env_show(const char * name)
{
  const char * v = std::getenv(name);
  return (v != nullptr && *v != '\0') ? std::string(v) : "(未设置)";
}

std::string hostname()
{
  char buf[256] = {0};
  if (gethostname(buf, sizeof(buf) - 1) != 0) {
    return "unknown";
  }
  return std::string(buf);
}

bool is_zenoh_rmw(const std::string & rmw_id)
{
  return rmw_id.find("zenoh") != std::string::npos;
}

/// 把 RMW 实现 id 翻译成人类可读的供应商 + 发现方式
std::string describe_transport(const std::string & rmw_id)
{
  if (is_zenoh_rmw(rmw_id)) {
    return "Eclipse Zenoh (非 DDS 传输) — 发现: Zenoh router gossip 会话";
  }
  if (rmw_id.find("fastrtps") != std::string::npos) {
    return "eProsima Fast DDS — 发现: DDS 组播 SPDP/SEDP";
  }
  if (rmw_id.find("cyclonedds") != std::string::npos) {
    return "Eclipse Cyclone DDS — 发现: DDS 组播 SPDP/SEDP";
  }
  if (rmw_id.find("connext") != std::string::npos) {
    return "RTI Connext DDS — 发现: DDS 组播 SPDP/SEDP";
  }
  return "未知 RMW 实现 — 发现方式未知";
}

}  // namespace

class RmwProbeNode : public rclcpp::Node
{
public:
  RmwProbeNode()
  : Node("rmw_probe_node")
  {
    // ━━━ 参数: 默认值全部来自环境变量, 实现"参数丢失也能跑" ━━━
    this->declare_parameter("mode", env_or("ZENOH_LAB_MODE", "pub"));
    this->declare_parameter("topic", env_or("ZENOH_LAB_TOPIC", "/zenoh_probe"));
    this->declare_parameter("rate_hz", std::stod(env_or("ZENOH_LAB_RATE_HZ", "2.0")));
    this->declare_parameter("label", env_or("ZENOH_LAB_LABEL", this->get_name()));
    this->declare_parameter("exit_after", 0.0);

    mode_ = this->get_parameter("mode").as_string();
    topic_ = this->get_parameter("topic").as_string();
    rate_hz_ = this->get_parameter("rate_hz").as_double();
    label_ = this->get_parameter("label").as_string();
    exit_after_ = this->get_parameter("exit_after").as_double();

    if (rate_hz_ <= 0.0) {
      rate_hz_ = 2.0;
    }
    // 话题名必须有前导 '/'
    if (!topic_.empty() && topic_[0] != '/') {
      topic_ = "/" + topic_;
    }

    // ━━━ 传输自述横幅 ━━━
    print_banner();

    // ━━━ 按模式建 pub / sub ━━━
    if (mode_ == "pub" || mode_ == "both") {
      publisher_ = this->create_publisher<std_msgs::msg::String>(topic_, 10);
      const auto period = std::chrono::duration<double>(1.0 / rate_hz_);
      timer_ = this->create_wall_timer(
        std::chrono::duration_cast<std::chrono::nanoseconds>(period),
        std::bind(&RmwProbeNode::publish_once, this));
      RCLCPP_INFO(
        this->get_logger(), "发布中: %s @ %.1f Hz", topic_.c_str(), rate_hz_);
    }

    if (mode_ == "sub" || mode_ == "both") {
      subscription_ = this->create_subscription<std_msgs::msg::String>(
        topic_, 10,
        std::bind(&RmwProbeNode::on_message, this, std::placeholders::_1));
      RCLCPP_INFO(
        this->get_logger(), "订阅中: %s (等待对端...)", topic_.c_str());
    }

    if (mode_ != "pub" && mode_ != "sub" && mode_ != "both") {
      RCLCPP_ERROR(
        this->get_logger(), "未知 mode: '%s' (可选 pub|sub|both)", mode_.c_str());
    }

    // ━━━ 每秒一行计数, 让"收到几条"有可读的时间序列 ━━━
    stats_timer_ = this->create_wall_timer(1s, std::bind(&RmwProbeNode::report_stats, this));

    // ━━━ 定时退出 (脚本据此断言) ━━━
    if (exit_after_ > 0.0) {
      exit_timer_ = this->create_wall_timer(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::duration<double>(exit_after_)),
        [this]() {
          // 摘要由 main() 在 spin 结束后统一打印, 这里只负责收工
          rclcpp::shutdown();
        });
      RCLCPP_INFO(this->get_logger(), "将在 %.1f 秒后自动退出", exit_after_);
    }
  }

  /// 给外部 (main) 在收到 Ctrl-C 时也能打出摘要
  void print_summary() const
  {
    RCLCPP_INFO(
      this->get_logger(),
      "SUMMARY mode=%s topic=%s sent=%lu received=%lu peer_rmw=%s rmw=%s domain=%s",
      mode_.c_str(), topic_.c_str(),
      static_cast<unsigned long>(sent_), static_cast<unsigned long>(received_),
      peer_rmw_.empty() ? "(none)" : peer_rmw_.c_str(),
      rmw_get_implementation_identifier(),
      env_show("ROS_DOMAIN_ID").c_str());
  }

private:
  void print_banner()
  {
    const std::string rmw_id = rmw_get_implementation_identifier();

    RCLCPP_INFO(this->get_logger(), "┌─ RMW Probe ──────────────────────────────────────────");
    RCLCPP_INFO(this->get_logger(), "│ 角色        : %s", mode_.c_str());
    RCLCPP_INFO(this->get_logger(), "│ 标签        : %s", label_.c_str());
    RCLCPP_INFO(this->get_logger(), "│ 话题        : %s @ %.1f Hz", topic_.c_str(), rate_hz_);
    RCLCPP_INFO(this->get_logger(), "│ ─────────── 传输层 ───────────");
    RCLCPP_INFO(this->get_logger(), "│ RMW 实现    : %s", rmw_id.c_str());
    RCLCPP_INFO(
      this->get_logger(), "│ RMW_IMPLEMENTATION : %s", env_show("RMW_IMPLEMENTATION").c_str());
    RCLCPP_INFO(this->get_logger(), "│ 传输        : %s", describe_transport(rmw_id).c_str());
    RCLCPP_INFO(this->get_logger(), "│ ROS_DOMAIN_ID      : %s", env_show("ROS_DOMAIN_ID").c_str());
    RCLCPP_INFO(
      this->get_logger(), "│ ROS_LOCALHOST_ONLY : %s", env_show("ROS_LOCALHOST_ONLY").c_str());
    RCLCPP_INFO(this->get_logger(), "│ 主机        : %s", hostname().c_str());

    if (is_zenoh_rmw(rmw_id)) {
      // Zenoh 专有: 会话/路由配置来源, 直接决定"连到哪个路由器"
      RCLCPP_INFO(this->get_logger(), "│ ─────────── Zenoh 会话 ───────────");
      RCLCPP_INFO(
        this->get_logger(), "│ ZENOH_SESSION_CONFIG_URI  : %s",
        env_show("ZENOH_SESSION_CONFIG_URI").c_str());
      RCLCPP_INFO(
        this->get_logger(), "│ ZENOH_ROUTER_CONFIG_URI   : %s",
        env_show("ZENOH_ROUTER_CONFIG_URI").c_str());
      RCLCPP_INFO(
        this->get_logger(), "│ ZENOH_CONFIG_OVERRIDE     : %s",
        env_show("ZENOH_CONFIG_OVERRIDE").c_str());
      RCLCPP_INFO(
        this->get_logger(), "│ ZENOH_ROUTER_CHECK_ATTEMPTS : %s",
        env_show("ZENOH_ROUTER_CHECK_ATTEMPTS").c_str());
    } else {
      RCLCPP_INFO(
        this->get_logger(),
        "│ 注: DDS 供应商之间靠 RTPS 线协议互通; Zenoh 不在此列 (键空间不同)");
    }
    RCLCPP_INFO(this->get_logger(), "└──────────────────────────────────────────────────────");
  }

  void publish_once()
  {
    auto msg = std::make_unique<std_msgs::msg::String>();
    // 载荷带 RMW id: 订阅端据此知道对端走的是哪条栈, 而不是靠猜
    msg->data = std::string("{")
      + "\"rmw\":\"" + rmw_get_implementation_identifier() + "\","
      + "\"label\":\"" + label_ + "\","
      + "\"seq\":" + std::to_string(sent_) + ","
      + "\"ts\":" + std::to_string(this->now().nanoseconds()) + ","
      + "\"domain\":\"" + env_show("ROS_DOMAIN_ID") + "\","
      + "\"host\":\"" + hostname() + "\""
      + "}";
    publisher_->publish(std::move(msg));
    sent_++;
  }

  void on_message(std_msgs::msg::String::ConstSharedPtr msg)
  {
    received_++;

    // 首条消息: 解析对端 RMW, 判断是不是跨栈/跨供应商
    if (received_ == 1) {
      const std::string my_rmw = rmw_get_implementation_identifier();
      const std::string key = "\"rmw\":\"";
      const auto pos = msg->data.find(key);
      if (pos != std::string::npos) {
        const auto begin = pos + key.size();
        const auto end = msg->data.find('"', begin);
        if (end != std::string::npos) {
          peer_rmw_ = msg->data.substr(begin, end - begin);
        }
      }
      RCLCPP_INFO(
        this->get_logger(), "✅ 首条消息到达 (等待期间 %.1f 秒)", wait_seconds());
      RCLCPP_INFO(this->get_logger(), "   本机 RMW : %s", my_rmw.c_str());
      RCLCPP_INFO(
        this->get_logger(), "   对端 RMW : %s", peer_rmw_.empty() ? "(未解析)" : peer_rmw_.c_str());
      if (!peer_rmw_.empty() && peer_rmw_ != my_rmw) {
        RCLCPP_INFO(
          this->get_logger(), "   → 跨实现通信成立 (两端 RMW 不同)");
      } else if (!peer_rmw_.empty()) {
        RCLCPP_INFO(this->get_logger(), "   → 同为 %s", peer_rmw_.c_str());
      }
    }

    RCLCPP_INFO_THROTTLE(
      this->get_logger(), *this->get_clock(), 2000,
      "收到 #%lu: %s", static_cast<unsigned long>(received_), msg->data.c_str());
  }

  void report_stats()
  {
    RCLCPP_INFO_THROTTLE(
      this->get_logger(), *this->get_clock(), 1000,
      "统计: sent=%lu received=%lu", static_cast<unsigned long>(sent_),
      static_cast<unsigned long>(received_));
  }

  double wait_seconds() const
  {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
  }

  std::string mode_;
  std::string topic_;
  std::string label_;
  double rate_hz_ = 2.0;
  double exit_after_ = 0.0;

  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::TimerBase::SharedPtr stats_timer_;
  rclcpp::TimerBase::SharedPtr exit_timer_;

  size_t sent_ = 0;
  size_t received_ = 0;
  std::string peer_rmw_;
  std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);

  auto node = std::make_shared<RmwProbeNode>();

  rclcpp::spin(node);

  // spin 结束 (Ctrl-C 或定时退出) 后再打一次摘要
  node->print_summary();

  rclcpp::shutdown();
  return 0;
}
