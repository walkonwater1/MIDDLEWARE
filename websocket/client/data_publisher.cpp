/**
 * data_publisher.cpp — 模拟数据推送器实现
 */

#include "data_publisher.h"

#include <chrono>
#include <cmath>
#include <iostream>

#include "common/ws_common.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ===========================================================================
// 构造/析构
// ===========================================================================

DataPublisher::DataPublisher()
    : running_(false),
      need_exit_(false),
      buf_(kBufferCapacity),
      stream_state_(PushStreamState::Idle),
      started_ms_(0) {}

DataPublisher::~DataPublisher() { Stop(); }

// ===========================================================================
// 公共 API
// ===========================================================================

bool DataPublisher::Initialize(const std::string& data_channel) {
  if (data_channel.empty()) {
    std::cerr << "[PUB] data_channel 为空" << std::endl;
    return false;
  }
  data_channel_ = data_channel;
  std::cout << "[PUB] 初始化, data_channel=" << data_channel_ << std::endl;
  return true;
}

bool DataPublisher::Start() {
  if (running_.load()) {
    std::cout << "[PUB] 已在运行" << std::endl;
    return true;
  }
  if (data_channel_.empty()) {
    std::cerr << "[PUB] 未初始化, 先调用 Initialize()" << std::endl;
    return false;
  }
  if (push_thread_.joinable()) push_thread_.join();

  need_exit_.store(false);
  SetStreamState(PushStreamState::Launching);
  buf_.Reset();
  push_thread_ = std::thread(&DataPublisher::PushLoop, this);
  std::cout << "[PUB] 推送线程已启动" << std::endl;
  return true;
}

void DataPublisher::Stop() {
  if (!running_.load() && !push_thread_.joinable()) return;
  std::cout << "[PUB] 停止推送..." << std::endl;
  need_exit_.store(true);
  buf_.CV().notify_all();
  KillFfmpeg();
  if (push_thread_.joinable()) push_thread_.join();
  SetStreamState(PushStreamState::Idle);
  std::cout << "[PUB] 已停止" << std::endl;
}

// ===========================================================================
// 喂数据 (模拟 AFE callback)
// ===========================================================================

void DataPublisher::FeedData(const uint8_t* data, std::size_t size) {
  if (!running_.load()) return;
  unsigned int written = buf_.Write(data, size);
  if (written > 0) buf_.NotifyOne();

  static int feed_count = 0;
  feed_count++;
  if (feed_count == 1) {
    std::cout << "[PUB] 首次数据到达: " << size << " bytes" << std::endl;
  }
  if (written < size) {
    std::cerr << "[PUB] 缓冲区满! 丢弃 " << (size - written) << " bytes" << std::endl;
  }
}

// ===========================================================================
// 推送循环 (后台线程)
// ===========================================================================

void DataPublisher::PushLoop() {
  running_.store(true);
  std::cout << "[PUB] 推送循环启动, 目标=" << data_channel_ << std::endl;

  reconnect_delay_ms_ = 2000;

  while (!need_exit_.load()) {
    std::cout << "[PUB] 启动 ffmpeg (模拟)..." << std::endl;
    if (LaunchFfmpeg()) {
      reconnect_delay_ms_ = 2000;
      started_ms_.store(JsonBuilder::now_ms());
      SetStreamState(PushStreamState::Streaming);

      auto start_time = std::chrono::steady_clock::now();
      std::vector<uint8_t> local_buf(4096);
      int push_loops = 0;

      while (!need_exit_.load()) {
        unsigned int read = buf_.ReadWait(local_buf.data(), local_buf.size(), 100);
        if (read == 0 && buf_.Empty() && need_exit_.load()) break;
        if (read == 0) continue;

        // 模拟: 数据传输 (实际 Demo 中数据仅做统计)
        push_loops++;
        if (push_loops % 50 == 0) {
          auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::steady_clock::now() - start_time).count();
          std::cout << "[PUB] 推送中: loops=" << push_loops
                    << ", buf_used=" << buf_.Used()
                    << "/" << buf_.Capacity()
                    << ", elapsed=" << elapsed << "s" << std::endl;
        }
      }
    } else {
      std::cerr << "[PUB] ffmpeg 启动失败" << std::endl;
      SetStreamState(PushStreamState::Error);
    }

    KillFfmpeg();
    if (need_exit_.load()) break;

    std::cout << "[PUB] 重连等待 " << reconnect_delay_ms_ << "ms..." << std::endl;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(reconnect_delay_ms_);
    while (!need_exit_.load() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    reconnect_delay_ms_ = std::min(reconnect_delay_ms_ * 2, reconnect_max_delay_ms_);
    if (!need_exit_.load()) SetStreamState(PushStreamState::Launching);
  }

  KillFfmpeg();
  running_.store(false);
  std::cout << "[PUB] 推送循环退出" << std::endl;
}

// ===========================================================================
// ffmpeg 管理 (模拟)
// ===========================================================================

bool DataPublisher::LaunchFfmpeg() {
  std::cout << "[PUB] ffmpeg 模拟启动 → " << data_channel_ << std::endl;
  return true;
}

void DataPublisher::KillFfmpeg() {
  std::cout << "[PUB] ffmpeg 模拟终止" << std::endl;
}

// ===========================================================================
// 状态机
// ===========================================================================

void DataPublisher::SetStreamState(PushStreamState state) {
  PushStreamState old = stream_state_.exchange(state);
  if (old != state) {
    const char* names[] = {"Idle", "Launching", "Streaming", "Reconnecting", "Error"};
    std::cout << "[PUB] 状态: " << names[static_cast<int>(old)]
              << " → " << names[static_cast<int>(state)] << std::endl;
    state_cv_.notify_all();
  }
}

PushStreamState DataPublisher::WaitForStreamReady(int timeout_ms) {
  PushStreamState current = stream_state_.load();
  if (current == PushStreamState::Streaming || current == PushStreamState::Error) {
    return current;
  }
  std::unique_lock<std::mutex> lock(state_mutex_);
  state_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
    auto s = stream_state_.load();
    return s == PushStreamState::Streaming || s == PushStreamState::Error;
  });
  return stream_state_.load();
}
