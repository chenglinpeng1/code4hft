#pragma once
// 延迟直方图: 落地性能基准与回归机制
// 对标 JD: p50/p99/p999 + T0 监控, 先有可量化的标尺再谈优化
#include <algorithm>
#include <cstdint>
#include <vector>

namespace hft {

class LatencyRecorder {
 public:
  void record(std::uint64_t ns) { samples_.push_back(ns); }
  void reset() { samples_.clear(); }
  std::size_t size() const { return samples_.size(); }

  struct Stats {
    std::uint64_t p50 = 0;
    std::uint64_t p99 = 0;
    std::uint64_t p999 = 0;
    std::uint64_t max = 0;
    double mean = 0;
  };

  // 非热路径调用: 排序求分位, 热路径只 record()
  Stats snapshot();

 private:
  std::vector<std::uint64_t> samples_;
};

}  // namespace hft
