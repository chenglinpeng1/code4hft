#pragma once
// FeedHandler: 收包 -> SPSC -> 重建 的最小生产级链路
// 对标 JD: epoll 收发 + 无锁队列 + OrderBook 重建 + seq 断层重订
//
// 线程模型: 1 收包(生产者, 调 on_wire) + 1 重建(消费者, 调 pump/run)
// 三级时间戳: ts_exchange(wire) -> ts_recv(on_wire 打点) -> ts_book(pump 打点)
// 时钟域说明: ts_exchange 与本地时钟不同源, wire-to-book 仅参考,
// queue_wait = ts_book - ts_recv 才是本机可比的排队时延。
#include <cstddef>
#include <cstdint>
#include <vector>

#include "hft/common/clock.hpp"
#include "hft/core/latency.hpp"
#include "hft/core/spsc_queue.hpp"
#include "hft/marketdata/itch_parser.hpp"
#include "hft/marketdata/order_book.hpp"

namespace hft {

struct FeedStats {
  std::uint64_t received = 0;    // on_wire 调用次数
  std::uint64_t decoded = 0;    // decode 成功数
  std::uint64_t decode_err = 0;
  std::uint64_t queue_full = 0;  // 背压丢数
  std::uint64_t consumed = 0;   // pump 弹出数
  std::uint64_t applied = 0;
  std::uint64_t gaps = 0;      // apply 返回 false 次数
  std::uint64_t resyncs = 0;   // 清书重订次数
  std::uint64_t snapshots = 0;  // apply_snapshot 次数
};

class FeedHandler {
 public:
  FeedHandler(SymbolId symbol, int depth = 10, std::size_t queue_pow2 = 4096);

  FeedHandler(const FeedHandler&) = delete;
  FeedHandler& operator=(const FeedHandler&) = delete;

  // 生产者(收包线程)调用: 解码 + ts_recv 打点 + 进队列
  // recv_ns 由调用方传入 now_ns(), 便于测试注入; 传 0 则内部打点
  bool on_wire(const char* p, std::size_t n, Nanos recv_ns = 0);

  // 消费者(重建线程)调用: 弹一个并 apply, 返回 false=队列空
  bool pump();

  // 排空队列(单测/回放用)
  std::size_t drain();

  OrderBook& book() { return book_; }
  const OrderBook& book() const { return book_; }
  const FeedStats& stats() const { return stats_; }
  LatencyRecorder& queue_wait() { return queue_wait_; }
  LatencyRecorder& apply_lat() { return apply_lat_; }

 private:
  bool apply_one(L2Tick& t);

  SpscQueue<L2Tick> q_;
  OrderBook book_;
  FeedStats stats_{};
  LatencyRecorder queue_wait_;  // ts_book - ts_recv
  LatencyRecorder apply_lat_;   // apply() 本体耗时
  bool need_snap_ = true;
  std::vector<L2Tick> snap_buf_;
};

}  // namespace hft
