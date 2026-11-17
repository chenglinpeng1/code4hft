#pragma once
// 无锁 SPSC 环形队列: 行情收包 -> OrderBook 重建线程的经典链路
// 对标 JD: 无锁队列 / 内存序 / 伪共享规避
//
// 约束: 单生产者单消费者, 容量须为 2 的幂, 值类型须 trivially copyable
// consistency: push 用 release, pop 用 acquire, size() 只做监控用
#include <atomic>
#include <cassert>
#include <cstddef>
#include <type_traits>

#include "hft/common/cacheline.hpp"

namespace hft {

template <typename T>
class SpscQueue {
  static_assert(std::is_trivially_copyable_v<T>,
                "SpscQueue<T> requires trivially copyable T for memcpy path");

 public:
  explicit SpscQueue(std::size_t capacity_pow2) {
    // 规整到 2 的幂
    std::size_t cap = 1;
    while (cap < capacity_pow2) cap <<= 1;
    cap_ = cap;
    mask_ = cap - 1;
    buf_ = static_cast<T*>(operator new[](cap * sizeof(T)));
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
  }

  ~SpscQueue() { operator delete[](buf_); }
  SpscQueue(const SpscQueue&) = delete;
  SpscQueue& operator=(const SpscQueue&) = delete;

  // 生产者: 满返回 false, 热路径无锁无系统调用
  bool push(const T& v) {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t next = head + 1;
    if (next - tail_.load(std::memory_order_acquire) > cap_) return false;
    buf_[head & mask_] = v;
    head_.store(next, std::memory_order_release);
    return true;
  }

  // 消费者: 空返回 false
  bool pop(T& out) {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return false;
    out = buf_[tail & mask_];
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  std::size_t capacity() const { return cap_; }
  bool empty() const {
    return tail_.load(std::memory_order_acquire) ==
           head_.load(std::memory_order_acquire);
  }

 private:
  T* buf_ = nullptr;
  std::size_t cap_ = 0;
  std::size_t mask_ = 0;
  HFT_CACHELINE_ALIGNED std::atomic<std::size_t> head_{0};  // 生产者独占
  HFT_CACHELINE_ALIGNED std::atomic<std::size_t> tail_{0};  // 消费者独占
};

}  // namespace hft
