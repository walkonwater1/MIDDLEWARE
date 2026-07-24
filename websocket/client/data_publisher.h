/**
 * data_publisher.h — 模拟数据推送器
 *
 * 模拟生产环境中的 RtspAudioPublisher:
 *   - 生成模拟"传感器"数据 (正弦波)
 *   - Ring buffer 缓冲
 *   - 后台线程消费写入 (模拟 ffmpeg pipe)
 *   - 状态机: Idle → Launching → Streaming → Reconnecting → Error
 *
 * 架构与生产代码一致, 但不依赖 ffmpeg, 数据直接丢弃 (Demo 用)。
 */

#ifndef DATA_PUBLISHER_H
#define DATA_PUBLISHER_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "common/ring_buffer.h"

enum class PushStreamState {
  Idle,
  Launching,
  Streaming,
  Reconnecting,
  Error,
};

class DataPublisher {
 public:
  DataPublisher();
  ~DataPublisher();

  DataPublisher(const DataPublisher&) = delete;
  DataPublisher& operator=(const DataPublisher&) = delete;

  // ---- 生命周期 ----

  /// @param data_channel 模拟的推流目标 (仅日志用)
  bool Initialize(const std::string& data_channel);

  /// 启动后台推送线程
  bool Start();

  /// 停止推送
  void Stop();

  bool IsRunning() const { return running_.load(); }
  PushStreamState GetState() const { return stream_state_.load(); }

  /// 阻塞等待 ffmpeg 到达 Streaming 状态
  PushStreamState WaitForStreamReady(int timeout_ms);

  /// ffmpeg 启动时刻 (epoch ms)
  int64_t GetStartedMs() const { return started_ms_.load(); }

  /// 从外部喂数据 (模拟 AFE callback)
  void FeedData(const uint8_t* data, std::size_t size);

 private:
  void PushLoop();
  bool LaunchFfmpeg();   // 模拟: 无实际操作, 仅记录日志
  void KillFfmpeg();     // 模拟: 无实际操作
  void SetStreamState(PushStreamState state);

  std::string data_channel_;

  std::atomic<bool> running_;
  std::atomic<bool> need_exit_;
  std::thread push_thread_;

  /// Ring buffer: 128K 容量 (~8s @ 16kHz 对于音频, 这里通用化为字节)
  static constexpr unsigned int kBufferCapacity = 128 * 1024;
  RingBuffer<uint8_t> buf_;

  int reconnect_delay_ms_ = 2000;
  int reconnect_max_delay_ms_ = 30000;

  std::atomic<PushStreamState> stream_state_;
  std::mutex state_mutex_;
  std::condition_variable state_cv_;

  std::atomic<int64_t> started_ms_;
};

#endif  // DATA_PUBLISHER_H
