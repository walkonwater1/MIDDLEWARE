/**
 * data_receiver.h — 模拟数据接收器
 *
 * 模拟生产环境中的 RtspAudioPuller:
 *   - 后台线程拉取数据
 *   - 支持流式 (重连) 和一次性 (One-shot) 模式
 *   - 统计接收字节数
 */

#ifndef DATA_RECEIVER_H
#define DATA_RECEIVER_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

class DataReceiver {
 public:
  DataReceiver();
  ~DataReceiver();

  DataReceiver(const DataReceiver&) = delete;
  DataReceiver& operator=(const DataReceiver&) = delete;

  /// @param url 数据源 URL (仅日志用)
  bool Initialize(const std::string& url);

  /// 启动后台拉流线程
  bool Start();

  /// 停止拉流
  void Stop();

  bool IsRunning() const { return running_.load(); }

  /// One-shot 模式: 完成一次拉取后退出 (模拟 HTTP 文件下载)
  void SetOneShot(bool one_shot) { one_shot_.store(one_shot); }

  /// 返回已接收的字节数
  int64_t GetBytesReceived() const { return bytes_received_.load(); }

 private:
  void PullLoop();
  bool LaunchFfmpeg();
  void KillFfmpeg();

  std::string url_;
  std::atomic<bool> running_;
  std::atomic<bool> need_exit_;
  std::atomic<bool> one_shot_{false};
  std::thread pull_thread_;
  std::atomic<int64_t> bytes_received_;
  int reconnect_delay_ms_ = 2000;
  int reconnect_max_delay_ms_ = 30000;
};

#endif  // DATA_RECEIVER_H
