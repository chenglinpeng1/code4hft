#pragma once
// SimEngine: 模拟撮合 + Order 生命周期 + 风控 + 持仓, 单线程正确优先
// 用法: engine.on_snapshot()/on_tick() 喂行情(复用 OrderBook),
//       engine.new_order() 下单(先风控再即时撮合), engine.cancel() 撤单,
//       每次行情更新后自动碰挂单 resting -> fills。
// 撮合语义(taker 视角, 不改变 book 本体):
//   买限价碰 ask<=px, 卖限价碰 bid>=px, 市价全扫对边; 限价残留挂起, 市价残留撤掉。
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "hft/common/clock.hpp"
#include "hft/marketdata/order_book.hpp"
#include "hft/trading/order.hpp"
#include "hft/trading/position.hpp"
#include "hft/trading/risk.hpp"

namespace hft {

struct EngineStats {
  std::uint64_t orders = 0;
  std::uint64_t rejected = 0;
  std::uint64_t cancelled = 0;
  std::uint64_t fills = 0;
};

class SimEngine {
 public:
  SimEngine(SymbolId symbol, RiskLimits lim = {});

  SimEngine(const SimEngine&) = delete;
  SimEngine& operator=(const SimEngine&) = delete;

  // 行情入口: 转调 OrderBook, 之后碰挂单
  void on_snapshot(const L2Tick* ticks, std::size_t n);
  // 返回 false=需重订(透传 OrderBook 语义), true 即使 gaps 也继续碰单
  bool on_tick(const L2Tick& t);

  // 下单: 返回订单指针(Rejected 也存档可查), fills 追加到 out_fills
  Order* new_order(Order o, std::vector<Fill>& out_fills);
  bool cancel(OrderId id);

  const Order* find(OrderId id) const;
  const Position& position() const { return pos_; }
  const OrderBook& book() const { return book_; }
  const EngineStats& stats() const { return stats_; }
  int open_orders() const { return open_orders_; }
  const std::vector<Fill>& all_fills() const { return fills_; }

 private:
  // 对 resting/limit/market 做即时撮合, 更新 order + position + fills_
  void match_now(Order& ord, std::vector<Fill>& out);
  void touch_resting(std::vector<Fill>& out);

  OrderBook book_;
  RiskChecker risk_;
  Position pos_;
  std::unordered_map<OrderId, Order> orders_;
  std::vector<Fill> fills_;
  EngineStats stats_{};
  int open_orders_ = 0;
  OrderId next_auto_id_ = 1;
};

}  // namespace hft
