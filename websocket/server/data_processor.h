/**
 * data_processor.h — 模拟数据处理引擎
 *
 * 模拟服务器侧的数据处理管线:
 *   1. 接收客户端推送的"数据" (stream_begin 后开始处理)
 *   2. 定期发送 process_result (模拟 ASR 识别)
 *   3. 在适当时机发送 data_notify (模拟 TTS 生成完成)
 *   4. 支持发送 stream_interrupt 中断
 */

#ifndef DATA_PROCESSOR_H
#define DATA_PROCESSOR_H

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

/// 处理器需要向客户端发送消息, 此回调用于注入发送能力
using SendToClientCallback =
    std::function<void(const std::string& session_id, const std::string& message)>;

class DataProcessor {
 public:
  DataProcessor();
  ~DataProcessor();

  DataProcessor(const DataProcessor&) = delete;
  DataProcessor& operator=(const DataProcessor&) = delete;

  /// 注入消息发送回调
  void SetSendCallback(SendToClientCallback cb) { send_cb_ = std::move(cb); }

  // ---- 客户端事件触发 ----

  /// 客户端已开始推流
  void OnStreamBegin(const std::string& session_id);

  /// 客户端流状态变更
  /// @param status "started" 或 "completed"
  void OnStreamState(const std::string& session_id, const std::string& status);

  /// 客户端断开
  void OnClientDisconnect(const std::string& session_id);

  // ---- 生命周期 ----

  void Start();
  void Stop();

 private:
  /// 后台处理循环: 定期向活跃会话发送模拟结果
  void ProcessLoop();

  SendToClientCallback send_cb_;

  struct SessionState {
    std::string session_id;
    bool streaming = false;       // 是否正在推流
    bool suppressed = false;      // 是否抑制处理 (模拟 VAD 抑制)
    int64_t started_at_ms = 0;
    int process_count = 0;
  };

  std::map<std::string, SessionState> sessions_;
  std::mutex sessions_mutex_;

  std::atomic<bool> running_;
  std::atomic<bool> need_exit_;
  std::thread process_thread_;
};

#endif  // DATA_PROCESSOR_H
