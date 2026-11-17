#pragma once
#include <cstddef>

namespace hft {

// x86/ARM 通用 cacheline 隔离, 防伪共享(对应 JD: 伪共享规避)
inline constexpr std::size_t kCacheLine = 64;

#define HFT_CACHELINE_ALIGNED alignas(hft::kCacheLine)

}  // namespace hft
