#include "hft/core/latency.hpp"

namespace hft {

LatencyRecorder::Stats LatencyRecorder::snapshot() {
  Stats s{};
  if (samples_.empty()) return s;
  std::sort(samples_.begin(), samples_.end());
  auto at = [&](double q) -> std::uint64_t {
    std::size_t idx = static_cast<std::size_t>(q * samples_.size());
    if (idx >= samples_.size()) idx = samples_.size() - 1;
    return samples_[idx];
  };
  s.p50 = at(0.50);
  s.p99 = at(0.99);
  s.p999 = at(0.999);
  s.max = samples_.back();
  long double sum = 0;
  for (auto v : samples_) sum += v;
  s.mean = static_cast<double>(sum / samples_.size());
  return s;
}

}  // namespace hft
