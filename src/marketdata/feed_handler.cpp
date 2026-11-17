#include "hft/marketdata/feed_handler.hpp"

namespace hft {

FeedHandler::FeedHandler(SymbolId symbol, int depth, std::size_t queue_pow2)
    : q_(queue_pow2), book_(symbol, depth) {}

bool FeedHandler::on_wire(const char* p, std::size_t n, Nanos recv_ns) {
  ++stats_.received;
  auto raw = decode_itch(p, n);
  if (!raw) {
    ++stats_.decode_err;
    return false;
  }
  L2Tick t = to_tick(*raw);
  t.ts_recv = (recv_ns != 0) ? recv_ns : now_ns();
  ++stats_.decoded;
  if (!q_.push(t)) {
    ++stats_.queue_full;
    return false;
  }
  return true;
}

bool FeedHandler::apply_one(L2Tick& t) {
  // 快照装配期: 攒 Snapshot 行, 首个增量到达时一次性 apply_snapshot
  if (need_snap_) {
    if (t.action == L2Action::Snapshot) {
      snap_buf_.push_back(t);
      return true;
    }
    if (!snap_buf_.empty()) {
      book_.apply_snapshot(snap_buf_.data(), snap_buf_.size());
      ++stats_.snapshots;
      need_snap_ = false;
      snap_buf_.clear();
    }
    // 无快照直接来增量: 下面的 book_.apply 会返回 false, 走重订路径
  }

  const std::uint64_t s = now_ns();
  const bool ok = book_.apply(t);
  const std::uint64_t e = now_ns();
  apply_lat_.record(e - s);
  if (ok) {
    ++stats_.applied;
  } else {
    ++stats_.gaps;
    // 脏数据/断层/溢出: 切回装配期等下一轮快照, 实盘这里接告警
    if (!need_snap_) ++stats_.resyncs;
    need_snap_ = true;
    snap_buf_.clear();
  }
  return ok;
}

bool FeedHandler::pump() {
  L2Tick t{};
  if (!q_.pop(t)) return false;
  ++stats_.consumed;
  t.ts_book = now_ns();  // 消费者回填, 不经过队列, 无内存序问题
  if (t.ts_book >= t.ts_recv) queue_wait_.record(t.ts_book - t.ts_recv);
  apply_one(t);
  return true;
}

std::size_t FeedHandler::drain() {
  std::size_t n = 0;
  while (pump()) ++n;
  return n;
}

}  // namespace hft
