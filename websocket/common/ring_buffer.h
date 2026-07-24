/**
 * ring_buffer.h — 线程安全环形缓冲区
 *
 * 固定容量 (2^N), 单生产者单消费者 (SPSC) 安全。
 * 支持阻塞等待和非阻塞读写。
 */

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

template <typename T>
class RingBuffer {
 public:
  /// @param capacity 必须是 2 的幂
  explicit RingBuffer(unsigned int capacity)
      : capacity_(capacity),
        mask_(capacity - 1),
        buf_(capacity),
        write_pos_(0),
        read_pos_(0) {}

  RingBuffer(const RingBuffer&) = delete;
  RingBuffer& operator=(const RingBuffer&) = delete;

  // ---- 生产者接口 (单线程安全) ----

  /// 写入单个元素 (非阻塞), 返回实际写入数量 (0 或 1)
  unsigned int Write(const T& item) {
    unsigned int used = write_pos_ - read_pos_;
    if (used >= capacity_) return 0;
    buf_[write_pos_ & mask_] = item;
    write_pos_++;
    return 1;
  }

  /// 批量写入 (非阻塞), 返回实际写入数量
  unsigned int Write(const T* items, unsigned int count) {
    unsigned int used = write_pos_ - read_pos_;
    unsigned int free_slots = capacity_ - used;
    unsigned int to_write = (count < free_slots) ? count : free_slots;
    for (unsigned int i = 0; i < to_write; i++) {
      buf_[(write_pos_ + i) & mask_] = items[i];
    }
    write_pos_ += to_write;
    return to_write;
  }

  /// 通知等待的消费者
  void NotifyOne() { cv_.notify_one(); }
  void NotifyAll() { cv_.notify_all(); }

  // ---- 消费者接口 (单线程安全) ----

  /// 读取单个元素 (非阻塞), 返回实际读取数量 (0 或 1)
  unsigned int Read(T& out) {
    unsigned int used = write_pos_ - read_pos_;
    if (used == 0) return 0;
    out = buf_[read_pos_ & mask_];
    read_pos_++;
    return 1;
  }

  /// 批量读取 (非阻塞), 返回实际读取数量
  unsigned int Read(T* out, unsigned int count) {
    unsigned int used = write_pos_ - read_pos_;
    unsigned int to_read = (count < used) ? count : used;
    for (unsigned int i = 0; i < to_read; i++) {
      out[i] = buf_[(read_pos_ + i) & mask_];
    }
    read_pos_ += to_read;
    return to_read;
  }

  /// 阻塞等待并读取 (超时 ms), 返回实际读取数量
  unsigned int ReadWait(T* out, unsigned int count, int timeout_ms) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                   [this] { return (write_pos_ - read_pos_) > 0; });
    }
    return Read(out, count);
  }

  // ---- 状态查询 ----
  unsigned int Used() const { return write_pos_ - read_pos_; }
  unsigned int Free() const { return capacity_ - Used(); }
  unsigned int Capacity() const { return capacity_; }
  bool Empty() const { return Used() == 0; }
  void Reset() {
    write_pos_ = 0;
    read_pos_ = 0;
  }

  // 暴露互斥锁和条件变量给外部 (用于手动阻塞等待)
  std::mutex& Mutex() { return mutex_; }
  std::condition_variable& CV() { return cv_; }

 private:
  const unsigned int capacity_;
  const unsigned int mask_;
  std::vector<T> buf_;
  std::atomic<unsigned int> write_pos_;
  std::atomic<unsigned int> read_pos_;
  std::mutex mutex_;
  std::condition_variable cv_;
};

#endif  // RING_BUFFER_H
