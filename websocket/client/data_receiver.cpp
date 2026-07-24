/**
 * data_receiver.cpp — 模拟数据接收器实现
 */

#include "data_receiver.h"

#include <chrono>
#include <iostream>
#include <random>

// ===========================================================================
// 构造/析构
// ===========================================================================

DataReceiver::DataReceiver()
    : running_(false), need_exit_(false), bytes_received_(0) {}

DataReceiver::~DataReceiver() { Stop(); }

// ===========================================================================
// 公共 API
// ===========================================================================

bool DataReceiver::Initialize(const std::string& url) {
  if (url.empty()) {
    std::cerr << "[RECV] URL 为空" << std::endl;
    return false;
  }
  url_ = url;
  std::cout << "[RECV] 初始化, url=" << url_ << std::endl;
  return true;
}

bool DataReceiver::Start() {
  if (running_.load()) {
    std::cout << "[RECV] 已在运行" << std::endl;
    return true;
  }
  if (url_.empty()) {
    std::cerr << "[RECV] 未初始化" << std::endl;
    return false;
  }
  if (pull_thread_.joinable()) pull_thread_.join();

  need_exit_.store(false);
  bytes_received_.store(0);
  pull_thread_ = std::thread(&DataReceiver::PullLoop, this);
  std::cout << "[RECV] 拉流线程已启动" << std::endl;
  return true;
}

void DataReceiver::Stop() {
  if (!running_.load() && !pull_thread_.joinable()) return;
  std::cout << "[RECV] 停止拉流..." << std::endl;
  need_exit_.store(true);
  KillFfmpeg();
  if (pull_thread_.joinable()) pull_thread_.join();
  std::cout << "[RECV] 已停止" << std::endl;
}

// ===========================================================================
// 拉流循环
// ===========================================================================

void DataReceiver::PullLoop() {
  running_.store(true);
  std::cout << "[RECV] 拉流循环启动, url=" << url_ << std::endl;

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> size_dist(256, 2048);

  reconnect_delay_ms_ = 2000;

  while (!need_exit_.load()) {
    std::cout << "[RECV] 启动 ffmpeg 拉流 (模拟)..." << std::endl;
    if (LaunchFfmpeg()) {
      reconnect_delay_ms_ = 2000;
      int chunks = 0;

      while (!need_exit_.load()) {
        // 模拟: 每 500ms 收到一块数据
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        int chunk_size = size_dist(gen);
        bytes_received_.fetch_add(chunk_size);
        chunks++;

        if (chunks % 10 == 0) {
          std::cout << "[RECV] 拉流中: " << chunks << " chunks, "
                    << bytes_received_.load() << " bytes" << std::endl;
        }

        // One-shot: 20 个 chunk 后退出
        if (one_shot_.load() && chunks >= 20) {
          std::cout << "[RECV] One-shot 完成: " << bytes_received_.load()
                    << " bytes" << std::endl;
          break;
        }

        // 流模式: 50 个 chunk 后模拟断开重连
        if (!one_shot_.load() && chunks >= 50) {
          std::cout << "[RECV] 流断开 (模拟), 准备重连..." << std::endl;
          break;
        }
      }
    } else {
      std::cerr << "[RECV] ffmpeg 启动失败" << std::endl;
    }

    KillFfmpeg();
    if (need_exit_.load()) break;
    if (one_shot_.load()) break;  // One-shot 不重连

    std::cout << "[RECV] 重连等待 " << reconnect_delay_ms_ << "ms..." << std::endl;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(reconnect_delay_ms_);
    while (!need_exit_.load() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    reconnect_delay_ms_ = std::min(reconnect_delay_ms_ * 2, reconnect_max_delay_ms_);
  }

  running_.store(false);
  std::cout << "[RECV] 拉流循环退出, 共接收 " << bytes_received_.load()
            << " bytes" << std::endl;
}

// ===========================================================================
// ffmpeg 管理 (模拟)
// ===========================================================================

bool DataReceiver::LaunchFfmpeg() {
  std::cout << "[RECV] ffmpeg 模拟启动 ← " << url_ << std::endl;
  return true;
}

void DataReceiver::KillFfmpeg() {
  std::cout << "[RECV] ffmpeg 模拟终止" << std::endl;
}
