// bench_orderbook: apply() 热路径压测, 先有 p50/p99 再谈优化
// 口径: warmup 5 万次(去冷启动/分支预测), 正式 50 万次混合操作
// (Change 同价 70% / New+Delete 轮换 30%), 每 op 两次 now_ns() 计入开销,
// p50=0 在 Mac 上是 steady_clock 粒度所致, 看 p99/mean。
#include <cstdio>

#include "hft/common/clock.hpp"
#include "hft/core/latency.hpp"
#include "hft/marketdata/order_book.hpp"

using namespace hft;

int main() {
  constexpr int kWarmup = 50000;
  constexpr int kIters = 500000;
  OrderBook book(1, 10);
  L2Tick snap[4] = {};
  snap[0] = {.symbol = 1, .seq = 1, .side = Side::Bid, .action = L2Action::Snapshot, .price = 10000, .qty = 1000};
  snap[1] = {.symbol = 1, .seq = 1, .side = Side::Bid, .action = L2Action::Snapshot, .price = 9990, .qty = 500};
  snap[2] = {.symbol = 1, .seq = 1, .side = Side::Ask, .action = L2Action::Snapshot, .price = 10010, .qty = 1000};
  snap[3] = {.symbol = 1, .seq = 1, .side = Side::Ask, .action = L2Action::Snapshot, .price = 10020, .qty = 500};
  book.apply_snapshot(snap, 4);

  SeqNo seq = 2;
  auto make_tick = [&](int i, SeqNo s) {
    L2Tick t{.symbol = 1, .seq = s, .qty = 1000 + (i % 16)};
    const int m = i % 10;
    if (m < 7) {
      // 70%: 同价 Change, 命中数组首元素, 最快路径
      t.side = (i % 2 ? Side::Bid : Side::Ask);
      t.action = L2Action::Change;
      t.price = (i % 2 ? 10000 : 10010);
    } else if (m < 8) {
      // 10%: 新价 New, 走 lower_bound + memmove
      t.side = Side::Bid;
      t.action = L2Action::New;
      t.price = 9980 - (i % 5);
      t.qty = 10;
    } else if (m < 9) {
      // 10%: 删刚加的档, 走 find + memmove
      t.side = Side::Bid;
      t.action = L2Action::Delete;
      t.price = 9980 - ((i - 1) % 5);
    } else {
      // 10%: Trade 吃量, 走 find
      t.side = Side::Ask;
      t.action = L2Action::Trade;
      t.price = 10010;
      t.qty = 1;
    }
    return t;
  };

  for (int i = 0; i < kWarmup; ++i) {
    L2Tick t = make_tick(i, seq++);
    // warmup 允许 New/Delete 的 seq 连续失败? 不会: 每个 op seq+1, 必成功
    if (!book.apply(t)) {
      std::printf("warmup apply failed at %d\n", i);
      return 1;
    }
  }
  // warmup 后清书重建, 保证正式段从干净的 4 档书开始
  book.clear();
  book.apply_snapshot(snap, 4);
  seq = 2;

  LatencyRecorder rec;
  for (int i = 0; i < kIters; ++i) {
    L2Tick t = make_tick(i, seq++);
    std::uint64_t s = now_ns();
    bool ok = book.apply(t);
    std::uint64_t e = now_ns();
    if (!ok) {
      std::printf("apply failed at %d\n", i);
      return 1;
    }
    rec.record(e - s);
  }
  auto st = rec.snapshot();
  std::printf("iters=%d p50=%llu p99=%llu p999=%llu max=%llu mean=%.1f (ns) drops=%llu\n",
              kIters, (unsigned long long)st.p50, (unsigned long long)st.p99,
              (unsigned long long)st.p999, (unsigned long long)st.max, st.mean,
              (unsigned long long)book.drops());
  return 0;
}
