// replay: 回放 32B ITCH-like 文件 -> OrderBook, 统计 apply 时延
// 用法: ./replay <data.bin> ; 无参数则跑内置合成流
#include <cinttypes>
#include <cstdio>
#include <fstream>
#include <vector>

#include "hft/common/clock.hpp"
#include "hft/core/latency.hpp"
#include "hft/marketdata/itch_parser.hpp"
#include "hft/marketdata/order_book.hpp"

using namespace hft;

static void run_ticks(const std::vector<L2Tick>& ticks) {
  if (ticks.empty()) {
    std::printf("empty stream\n");
    return;
  }
  OrderBook book(ticks[0].symbol, 10);
  LatencyRecorder rec;
  std::size_t gaps = 0, applied = 0;
  bool need_snap = true;
  std::vector<L2Tick> snap_buf;
  for (auto& t : ticks) {
    if (t.action == L2Action::Snapshot && need_snap) {
      snap_buf.push_back(t);
      continue;
    }
    if (need_snap && !snap_buf.empty()) {
      book.apply_snapshot(snap_buf.data(), snap_buf.size());
      need_snap = false;
    }
    std::uint64_t s = now_ns();
    bool ok = book.apply(t);
    std::uint64_t e = now_ns();
    rec.record(e - s);
    if (!ok) {
      ++gaps;  // 断层/交叉: 实盘这里要清书重订快照
      need_snap = true;
      snap_buf.clear();
      continue;
    }
    ++applied;
  }
  auto st = rec.snapshot();
  auto bb = book.best_bid();
  auto ba = book.best_ask();
  std::printf("applied=%zu gaps=%zu p50=%llu p99=%llu p999=%llu max=%llu mean=%.1f (ns)\n",
              applied, gaps, (unsigned long long)st.p50,
              (unsigned long long)st.p99, (unsigned long long)st.p999,
              (unsigned long long)st.max, st.mean);
  std::printf("best_bid=%lld best_ask=%lld\n",
              bb ? (long long)*bb : -1, ba ? (long long)*ba : -1);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    // 合成流: 快照 4 档 + 10k 笔增量
    std::vector<L2Tick> v;
    v.reserve(10004);
    for (int i = 0; i < 2; ++i)
      v.push_back({.symbol = 1, .seq = 1, .side = Side::Bid, .action = L2Action::Snapshot, .price = 10000 - i * 10, .qty = 100});
    for (int i = 0; i < 2; ++i)
      v.push_back({.symbol = 1, .seq = 1, .side = Side::Ask, .action = L2Action::Snapshot, .price = 10010 + i * 10, .qty = 100});
    for (std::uint64_t i = 0; i < 10000; ++i)
      v.push_back({.symbol = 1, .seq = 2 + i, .side = (i % 2 ? Side::Bid : Side::Ask),
                   .action = L2Action::Change, .price = (i % 2 ? 10000 : 10010), .qty = 100 + (std::int64_t)(i % 7)});
    run_ticks(v);
    return 0;
  }
  std::ifstream f(argv[1], std::ios::binary);
  if (!f) {
    std::printf("open %s failed\n", argv[1]);
    return 1;
  }
  std::vector<L2Tick> v;
  char buf[kItchMsgLen];
  while (f.read(buf, kItchMsgLen)) {
    auto raw = decode_itch(buf, kItchMsgLen);
    if (!raw) continue;
    L2Tick t = to_tick(*raw);
    t.ts_recv = now_ns();
    v.push_back(t);
  }
  run_ticks(v);
  return 0;
}
