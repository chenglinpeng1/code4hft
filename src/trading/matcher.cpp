#include "hft/trading/matcher.hpp"

namespace hft {

SimEngine::SimEngine(SymbolId symbol, RiskLimits lim)
    : book_(symbol, 10), risk_(lim) {
  pos_.symbol = symbol;
}

void SimEngine::on_snapshot(const L2Tick* ticks, std::size_t n) {
  book_.apply_snapshot(ticks, n);
  std::vector<Fill> tmp;
  touch_resting(tmp);
}

bool SimEngine::on_tick(const L2Tick& t) {
  const bool ok = book_.apply(t);
  if (!ok) return false;
  std::vector<Fill> tmp;
  touch_resting(tmp);
  return true;
}

Order* SimEngine::new_order(Order o, std::vector<Fill>& out_fills) {
  if (o.id == 0) o.id = next_auto_id_++;
  if (o.id >= next_auto_id_) next_auto_id_ = o.id + 1;
  o.symbol = book_.symbol();
  o.leaves = o.qty;
  o.filled = 0;
  o.status = OrderStatus::New;
  o.reject_code = 0;
  if (o.ts_in == 0) o.ts_in = now_ns();

  const Qty delta = (o.side == Side::Bid) ? o.qty : -o.qty;
  Qty prospect = pos_.net + delta;
  if (prospect < 0) prospect = -prospect;
  const std::uint32_t rc = risk_.check(o, prospect, open_orders_);
  auto it = orders_.emplace(o.id, o).first;
  Order& stored = it->second;
  if (rc != 0) {
    stored.status = OrderStatus::Rejected;
    stored.reject_code = rc;
    ++stats_.rejected;
    return &stored;
  }
  stored.status = OrderStatus::Accepted;
  ++stats_.orders;
  match_now(stored, out_fills);
  return &stored;
}

bool SimEngine::cancel(OrderId id) {
  auto it = orders_.find(id);
  if (it == orders_.end()) return false;
  Order& o = it->second;
  if (o.status != OrderStatus::Resting && o.status != OrderStatus::Partial)
    return false;
  o.status = OrderStatus::Cancelled;
  --open_orders_;
  ++stats_.cancelled;
  return true;
}

const Order* SimEngine::find(OrderId id) const {
  auto it = orders_.find(id);
  return (it == orders_.end()) ? nullptr : &it->second;
}

void SimEngine::match_now(Order& ord, std::vector<Fill>& out) {
  std::vector<Level> opp = (ord.side == Side::Bid) ? book_.asks(OrderBook::kMaxLevels)
                                                  : book_.bids(OrderBook::kMaxLevels);
  for (const auto& lv : opp) {
    if (ord.leaves == 0) break;
    if (ord.type == OrderType::Limit) {
      if (ord.side == Side::Bid && lv.price > ord.price) break;
      if (ord.side == Side::Ask && lv.price < ord.price) break;
    }
    Qty take = (ord.leaves < lv.qty) ? ord.leaves : lv.qty;
    Fill f{ord.id, ord.symbol, ord.side, lv.price, take, now_ns()};
    out.push_back(f);
    fills_.push_back(f);
    pos_.apply_fill(f);
    ++stats_.fills;
    ord.leaves -= take;
    ord.filled += take;
  }
  if (ord.leaves == 0) {
    ord.status = OrderStatus::Filled;
  } else if (ord.type == OrderType::Market) {
    // IOC: 残留撤掉, 有成交也记 Cancelled(带 fills 即部分成交后撤余量)
    ord.status = OrderStatus::Cancelled;
    ++stats_.cancelled;
  } else {
    ord.status = (ord.filled > 0) ? OrderStatus::Partial : OrderStatus::Resting;
    ++open_orders_;
  }
}

void SimEngine::touch_resting(std::vector<Fill>& out) {
  for (auto& kv : orders_) {
    Order& o = kv.second;
    if (o.status != OrderStatus::Resting && o.status != OrderStatus::Partial)
      continue;
    // sim 简化: 不消耗 book 深度, 每个挂单看到同一本盘口(无限流动性假设)
    std::vector<Level> opp = (o.side == Side::Bid) ? book_.asks(OrderBook::kMaxLevels)
                                                  : book_.bids(OrderBook::kMaxLevels);
    bool touched = false;
    for (const auto& lv : opp) {
      if (o.leaves == 0) break;
      if (o.side == Side::Bid && lv.price > o.price) break;
      if (o.side == Side::Ask && lv.price < o.price) break;
      Qty take = (o.leaves < lv.qty) ? o.leaves : lv.qty;
      Fill f{o.id, o.symbol, o.side, lv.price, take, now_ns()};
      out.push_back(f);
      fills_.push_back(f);
      pos_.apply_fill(f);
      ++stats_.fills;
      o.leaves -= take;
      o.filled += take;
      touched = true;
    }
    if (touched && o.leaves == 0) {
      o.status = OrderStatus::Filled;
      --open_orders_;
    } else if (touched && o.status == OrderStatus::Resting && o.filled > 0) {
      o.status = OrderStatus::Partial;
    }
  }
}

}  // namespace hft
