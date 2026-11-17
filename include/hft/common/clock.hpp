#pragma once
#include <chrono>
#include <cstdint>

namespace hft {

// 统一纳秒时钟: 热路径用 steady_clock, 跨机器对时留给 ts_exchange
inline std::uint64_t now_ns() {
  return static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
}

#if defined(__x86_64__) || defined(__i386__)
inline std::uint64_t rdtsc() {
  std::uint32_t lo, hi;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return (static_cast<std::uint64_t>(hi) << 32) | lo;
}
#endif

}  // namespace hft
