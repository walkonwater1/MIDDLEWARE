/**
 * data_processor.cpp — 模拟数据处理引擎实现
 */

#include "data_processor.h"

#include <iostream>
#include <random>

#include "common/ws_common.h"

DataProcessor::DataProcessor() : running_(false), need_exit_(false) {}

DataProcessor::~DataProcessor() { Stop(); }

// ---------------------------------------------------------------------------
// 客户端事件触发
// ---------------------------------------------------------------------------

void DataProcessor::OnStreamBegin(const std::string& session_id) {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  auto& s = sessions_[session_id];
  s.session_id = session_id;
  s.streaming = true;
  s.started_at_ms = JsonBuilder::now_ms();
  s.process_count = 0;
  std::cout << "[PROC] 会话 " << session_id << " 开始接收数据流" << std::endl;
}

void DataProcessor::OnStreamState(const std::string& session_id,
                                   const std::string& status) {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) return;

  if (status == "started") {
    it->second.suppressed = true;
    std::cout << "[PROC] 会话 " << session_id << " 处理暂停 (抑制)" << std::endl;
  } else if (status == "completed") {
    it->second.suppressed = false;
    std::cout << "[PROC] 会话 " << session_id << " 处理恢复" << std::endl;
  }
}

void DataProcessor::OnClientDisconnect(const std::string& session_id) {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  sessions_.erase(session_id);
  std::cout << "[PROC] 会话 " << session_id << " 清理" << std::endl;
}

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------

void DataProcessor::Start() {
  running_.store(true);
  need_exit_.store(false);
  process_thread_ = std::thread(&DataProcessor::ProcessLoop, this);
  std::cout << "[PROC] 数据处理引擎已启动" << std::endl;
}

void DataProcessor::Stop() {
  need_exit_.store(true);
  if (process_thread_.joinable()) {
    process_thread_.join();
  }
  running_.store(false);
  std::cout << "[PROC] 数据处理引擎已停止" << std::endl;
}

// ---------------------------------------------------------------------------
// 处理循环
// ---------------------------------------------------------------------------

void DataProcessor::ProcessLoop() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> delay_dist(2000, 5000);  // 随机间隔 2-5s

  int loop_count = 0;

  while (!need_exit_.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    loop_count++;

    std::lock_guard<std::mutex> lock(sessions_mutex_);

    for (auto& [sid, state] : sessions_) {
      if (!state.streaming || state.suppressed) continue;

      state.process_count++;

      // 1. 发送处理进度 (模拟 ASR 中间结果)
      int progress = std::min(100, state.process_count * 15);
      std::string result_data =
          "\"progress\":" + std::to_string(progress) +
          ",\"text\":\"processed_chunk_" + std::to_string(state.process_count) + "\"";
      std::string msg =
          JsonBuilder::build_envelope(WsEvent::kProcessResult, sid, result_data);

      if (send_cb_) send_cb_(sid, msg);

      std::cout << "[PROC] → " << sid
                << " process_result (#" << state.process_count
                << ", progress=" << progress << "%)" << std::endl;

      // 2. 模拟: 第 3 次处理后发送 data_notify (模拟 TTS 就绪)
      if (state.process_count == 3) {
        std::string notify_data =
            "\"data_id\":\"data_" + sid + "_" +
            std::to_string(JsonBuilder::now_ms()) + "\"" +
            ",\"data_url\":\"http://localhost:" +
            std::to_string(WsConfig::kDefaultPort) + "/data/" + sid + ".raw\"" +
            ",\"size_bytes\":" + std::to_string(4096 + gen() % 8192);

        std::string notify_msg =
            JsonBuilder::build_envelope(WsEvent::kDataNotify, sid, notify_data);

        if (send_cb_) send_cb_(sid, notify_msg);
        std::cout << "[PROC] → " << sid << " data_notify" << std::endl;
      }

      // 3. 第 1 次处理时发送 inference_result
      if (state.process_count == 1) {
        std::string inference_data =
            "\"reply\":\"已收到数据流, 开始处理...\",\"confidence\":0.95";
        std::string inf_msg =
            JsonBuilder::build_envelope(WsEvent::kInferenceResult, sid, inference_data);
        if (send_cb_) send_cb_(sid, inf_msg);
        std::cout << "[PROC] → " << sid << " inference_result" << std::endl;
      }

      // 4. 每 5 次循环, 有概率发送 stream_interrupt
      if (loop_count % 5 == 0 && (gen() % 100) < 30) {
        std::string interrupt_msg =
            JsonBuilder::build_envelope(WsEvent::kStreamInterrupt, sid,
                                         "\"reason\":\"simulated_interrupt\"");
        if (send_cb_) send_cb_(sid, interrupt_msg);
        std::cout << "[PROC] → " << sid << " stream_interrupt (模拟)" << std::endl;
        state.suppressed = true;  // 打断后暂停处理
      }

      // 5. 第 2 次处理时发送 tip_message
      if (state.process_count == 2) {
        std::string tip_msg =
            JsonBuilder::build_envelope(WsEvent::kTipMessage, sid,
                                         "\"content\":\"处理进度: 25%\",\"duration\":2000");
        if (send_cb_) send_cb_(sid, tip_msg);
      }
    }
  }
}
