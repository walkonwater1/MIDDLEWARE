/**
 * z_sysmon.cpp — 系统状态采集发布端 (真实数据, 不是模拟值)
 *
 * 这是把 Zenoh 从"能发消息"推进到"能研究通信"的第一块砖:
 * 采集 H618 上真实存在的 /proc 与 /sys 数据, 按 1 Hz 发布三路 key。
 *
 *   <prefix>/cpu           CPU 使用率、load average、进程数、uptime
 *   <prefix>/memory        内存总量/可用/使用率
 *   <prefix>/temperature   所有 thermal zone 的温度 + 最高温
 *
 * 默认前缀 h618/system, 即:
 *   h618/system/cpu
 *   h618/system/memory
 *   h618/system/temperature
 *
 * 三条消息共用同一个 seq 与 ts_ns —— 订阅端可以据此把同一时刻的
 * CPU/内存/温度对齐 (这也是后面统计延迟、丢包的基础)。
 *
 * 用法:
 *   ./z_sysmon [prefix] [rate_hz] [count] [connect_endpoint]
 *   ./z_sysmon                                  # 1 Hz, 一直发
 *   ./z_sysmon h618/system 1 10                 # 发 10 个采样点
 *   ./z_sysmon h618/system 1 0 tcp/192.168.10.1:7447   # client 模式
 */

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "common.hxx"

namespace {

namespace fs = std::filesystem;

struct CpuTimes {
  unsigned long long user = 0, nice = 0, system = 0, idle = 0;
  unsigned long long iowait = 0, irq = 0, softirq = 0, steal = 0;

  unsigned long long total() const
  {
    return user + nice + system + idle + iowait + irq + softirq + steal;
  }
  unsigned long long idle_total() const { return idle + iowait; }
};

bool read_cpu_times(CpuTimes & out)
{
  std::ifstream f("/proc/stat");
  if (!f) {
    return false;
  }
  std::string label;
  return static_cast<bool>(
    f >> label >> out.user >> out.nice >> out.system >> out.idle >>
      out.iowait >> out.irq >> out.softirq >> out.steal) && label == "cpu";
}

struct MemInfo {
  unsigned long long total_kb = 0;
  unsigned long long available_kb = 0;
};

bool read_meminfo(MemInfo & out)
{
  std::ifstream f("/proc/meminfo");
  if (!f) {
    return false;
  }
  std::string line;
  while (std::getline(f, line)) {
    unsigned long long kb = 0;
    if (std::sscanf(line.c_str(), "MemTotal: %llu", &kb) == 1) {
      out.total_kb = kb;
    } else if (std::sscanf(line.c_str(), "MemAvailable: %llu", &kb) == 1) {
      out.available_kb = kb;
    }
  }
  return out.total_kb > 0;
}

struct ThermalZone {
  std::string type;
  double temp_c = 0.0;
};

std::vector<ThermalZone> read_thermal_zones()
{
  std::vector<ThermalZone> zones;
  std::error_code ec;
  for (const auto & entry : fs::directory_iterator("/sys/class/thermal", ec)) {
    const std::string name = entry.path().filename().string();
    if (name.rfind("thermal_zone", 0) != 0) {
      continue;
    }
    std::ifstream tf(entry.path() / "type");
    std::ifstream vf(entry.path() / "temp");
    std::string type;
    long long milli = 0;
    if (!(tf >> type) || !(vf >> milli)) {
      continue;
    }
    // kernel 以千分之一摄氏度导出, 例如 56762 → 56.762 °C
    zones.push_back({type, static_cast<double>(milli) / 1000.0});
  }
  std::sort(zones.begin(), zones.end(),
            [](const ThermalZone & a, const ThermalZone & b) { return a.type < b.type; });
  return zones;
}

bool read_loadavg(double & l1, double & l5, double & l15)
{
  std::ifstream f("/proc/loadavg");
  return static_cast<bool>(f >> l1 >> l5 >> l15);
}

double read_uptime_s()
{
  std::ifstream f("/proc/uptime");
  double up = 0.0;
  f >> up;
  return up;
}

int count_processes()
{
  int n = 0;
  std::error_code ec;
  for (const auto & entry : fs::directory_iterator("/proc", ec)) {
    const std::string name = entry.path().filename().string();
    if (!name.empty() &&
        std::all_of(name.begin(), name.end(),
                    [](unsigned char c) { return std::isdigit(c) != 0; })) {
      ++n;
    }
  }
  return n;
}

long long now_ns()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
           std::chrono::system_clock::now().time_since_epoch())
    .count();
}

void publish_json(zenoh::Publisher & publisher, const std::string & payload)
{
  zenoh::Publisher::PutOptions opts;
  opts.encoding = zenoh::Encoding::Predefined::application_json();
  publisher.put(zenoh::Bytes(payload), std::move(opts));
}

std::string cpu_payload(
  long seq, long long ts_ns, const std::string & host, double usage_pct,
  double l1, double l5, double l15, int procs, double uptime_s)
{
  std::ostringstream os;
  os << std::fixed << std::setprecision(1);
  os << "{\"seq\":" << seq
     << ",\"ts_ns\":" << ts_ns
     << ",\"host\":\"" << host << "\""
     << ",\"usage_pct\":" << usage_pct
     << std::setprecision(2)
     << ",\"load1\":" << l1
     << ",\"load5\":" << l5
     << ",\"load15\":" << l15
     << ",\"procs\":" << procs
     << ",\"uptime_s\":" << static_cast<long long>(uptime_s)
     << "}";
  return os.str();
}

std::string memory_payload(
  long seq, long long ts_ns, const std::string & host, const MemInfo & mem)
{
  const double total_mb = static_cast<double>(mem.total_kb) / 1024.0;
  const double avail_mb = static_cast<double>(mem.available_kb) / 1024.0;
  const double used_mb = total_mb - avail_mb;
  const double pct = total_mb > 0.0 ? 100.0 * used_mb / total_mb : 0.0;

  std::ostringstream os;
  os << std::fixed << std::setprecision(1);
  os << "{\"seq\":" << seq
     << ",\"ts_ns\":" << ts_ns
     << ",\"host\":\"" << host << "\""
     << ",\"total_mb\":" << static_cast<long long>(std::llround(total_mb))
     << ",\"available_mb\":" << static_cast<long long>(std::llround(avail_mb))
     << ",\"used_mb\":" << static_cast<long long>(std::llround(used_mb))
     << ",\"usage_pct\":" << pct
     << "}";
  return os.str();
}

std::string temperature_payload(
  long seq, long long ts_ns, const std::string & host,
  const std::vector<ThermalZone> & zones)
{
  std::ostringstream os;
  os << std::fixed << std::setprecision(1);
  os << "{\"seq\":" << seq
     << ",\"ts_ns\":" << ts_ns
     << ",\"host\":\"" << host << "\"";

  if (zones.empty()) {
    os << ",\"max_c\":null,\"zones\":[]}";
    return os.str();
  }

  double max_c = zones.front().temp_c;
  for (const auto & z : zones) {
    max_c = std::max(max_c, z.temp_c);
  }
  os << ",\"max_c\":" << max_c << ",\"zones\":[";
  for (std::size_t i = 0; i < zones.size(); ++i) {
    if (i != 0) {
      os << ",";
    }
    os << "{\"type\":\"" << zones[i].type << "\",\"temp_c\":" << zones[i].temp_c << "}";
  }
  os << "]}";
  return os.str();
}

}  // namespace

int main(int argc, char ** argv)
{
  const std::string prefix = (argc > 1) ? argv[1] : "h618/system";
  const double rate_hz = (argc > 2) ? std::atof(argv[2]) : 1.0;
  const long count = (argc > 3) ? std::atol(argv[3]) : 0;
  const std::string connect = (argc > 4) ? argv[4] : "";

  demo::install_sigint();
  demo::banner(
    "系统状态采集端 (z_sysmon)",
    prefix + "/{cpu,memory,temperature}", connect);

  auto session = demo::open_session(connect);

  // 三路 key 分开声明: 订阅端可以只订 cpu, 也可以用 h618/** 一次收全。
  auto cpu_pub = session.declare_publisher(zenoh::KeyExpr(prefix + "/cpu"));
  auto mem_pub = session.declare_publisher(zenoh::KeyExpr(prefix + "/memory"));
  auto temp_pub = session.declare_publisher(zenoh::KeyExpr(prefix + "/temperature"));

  std::cout << "[SYSMON] 已声明 "
            << prefix << "/{cpu,memory,temperature}, "
            << rate_hz << " Hz, count="
            << (count == 0 ? std::string("∞") : std::to_string(count)) << std::endl;

  CpuTimes prev;
  if (!read_cpu_times(prev)) {
    std::cerr << "[SYSMON] 读取 /proc/stat 失败, 无法计算 CPU 使用率" << std::endl;
  }

  const auto period =
    std::chrono::duration<double>(1.0 / (rate_hz > 0.0 ? rate_hz : 1.0));
  auto next = std::chrono::steady_clock::now();
  long seq = 0;

  while (!demo::g_stop && (count == 0 || seq < count)) {
    next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
    std::this_thread::sleep_until(next);
    if (demo::g_stop) {
      break;
    }

    const long long ts = now_ns();
    const std::string host = demo::hostname();

    // ── CPU: 用两次 /proc/stat 的差值, 而不是瞬时值 ──────────
    CpuTimes cur = prev;
    read_cpu_times(cur);
    double usage_pct = 0.0;
    const auto d_total = cur.total() - prev.total();
    const auto d_idle = cur.idle_total() - prev.idle_total();
    if (d_total > 0) {
      usage_pct = 100.0 * (1.0 - static_cast<double>(d_idle) / static_cast<double>(d_total));
      usage_pct = std::clamp(usage_pct, 0.0, 100.0);
    }
    prev = cur;

    double l1 = 0.0, l5 = 0.0, l15 = 0.0;
    read_loadavg(l1, l5, l15);
    const int procs = count_processes();
    const double uptime_s = read_uptime_s();

    // ── 内存 ────────────────────────────────────────────────
    MemInfo mem;
    if (!read_meminfo(mem)) {
      std::cerr << "[SYSMON] 读取 /proc/meminfo 失败" << std::endl;
    }

    // ── 温度: 所有 thermal zone ─────────────────────────────
    const auto zones = read_thermal_zones();

    const std::string cpu_json =
      cpu_payload(seq, ts, host, usage_pct, l1, l5, l15, procs, uptime_s);
    const std::string mem_json = memory_payload(seq, ts, host, mem);
    const std::string temp_json = temperature_payload(seq, ts, host, zones);

    publish_json(cpu_pub, cpu_json);
    publish_json(mem_pub, mem_json);
    publish_json(temp_pub, temp_json);

    double max_c = 0.0;
    for (const auto & z : zones) {
      max_c = std::max(max_c, z.temp_c);
    }
    std::cout << "[SYSMON] #" << seq
              << "  cpu=" << std::fixed << std::setprecision(1) << usage_pct
              << "%  mem_used=" << std::llround(static_cast<double>(mem.total_kb - mem.available_kb) / 1024.0)
              << "MB  max_temp=" << max_c << "C"
              << "  zones=" << zones.size() << std::endl;

    ++seq;
  }

  std::cout << "SUMMARY role=sysmon prefix=" << prefix << " ticks=" << seq
            << " messages=" << (seq * 3) << std::endl;
  return 0;
}
