#include <cstring>

#include "hft/marketdata/order_book.hpp"

namespace hft {

OrderBook::OrderBook(SymbolId symbol, int depth)
    : symbol_(symbol), depth_(depth) {}

void OrderBook::clear() {
  bid_size_ = 0;
  ask_size_ = 0;
  last_seq_ = 0;
  has_snapshot_ = false;
  drops_ = 0;
}

int OrderBook::find_level(const Level* a, int n, PriceTick px) const {
  for (int i = 0; i < n; ++i) {
    if (a[i].price == px) return i;
  }
  return -1;
}

int OrderBook::lower_bound_desc(const Level* a, int n, PriceTick px) const {
  int i = 0;
  while (i < n && a[i].price > px) ++i;
  return i;
}

int OrderBook::lower_bound_asc(const Level* a, int n, PriceTick px) const {
  int i = 0;
  while (i < n && a[i].price < px) ++i;
  return i;
}

void OrderBook::erase_at(Level* a, int& n, int idx) {
  if (idx < 0 || idx >= n) return;
  const int tail = n - idx - 1;
  if (tail > 0) std::memmove(&a[idx], &a[idx + 1], static_cast<std::size_t>(tail) * sizeof(Level));
  --n;
}

bool OrderBook::upsert(Level* a, int& n, bool is_bid, PriceTick px, Qty qty,
                       bool add) {
  const int idx = find_level(a, n, px);
  if (idx >= 0) {
    if (add)
      a[idx].qty += qty;
    else
      a[idx].qty = qty;
    if (a[idx].qty <= 0) erase_at(a, n, idx);
    return true;
  }
  if (qty <= 0) return true;  // 空量插入=空操作, 不算溢出
  if (n >= kMaxLevels) {
    ++drops_;
    return false;  // 容量溢出: 调用方必须重订快照
  }
  const int pos = is_bid ? lower_bound_desc(a, n, px) : lower_bound_asc(a, n, px);
  const int tail = n - pos;
  if (tail > 0) std::memmove(&a[pos + 1], &a[pos], static_cast<std::size_t>(tail) * sizeof(Level));
  a[pos].price = px;
  a[pos].qty = qty;
  ++n;
  return true;
}

bool OrderBook::erase_px(Level* a, int& n, PriceTick px) {
  const int idx = find_level(a, n, px);
  if (idx < 0) return true;  // 删不存在=空操作(交易所偶发), 不算错
  erase_at(a, n, idx);
  return true;
}

bool OrderBook::consume(Level* a, int& n, PriceTick px, Qty qty) {
  const int idx = find_level(a, n, px);
  if (idx < 0) return true;  // 成交打在空档=脏数据容忍, 不污染书
  if (a[idx].qty <= qty)
    erase_at(a, n, idx);
  else
    a[idx].qty -= qty;
  return true;
}

void OrderBook::apply_snapshot(const L2Tick* ticks, std::size_t n) {
  bid_size_ = 0;
  ask_size_ = 0;
  SeqNo mx = 0;
  // 快照行可能乱序/同价多行: 先收敛同价再排序插入, 保证数组有序
  for (std::size_t i = 0; i < n; ++i) {
    const auto& t = ticks[i];
    if (t.symbol != symbol_) continue;
    if (t.qty <= 0) continue;
    if (t.side == Side::Bid)
      upsert(bid_, bid_size_, true, t.price, t.qty, false);
    else
      upsert(ask_, ask_size_, false, t.price, t.qty, false);
    if (t.seq > mx) mx = t.seq;
  }
  last_seq_ = mx;
  has_snapshot_ = (n > 0);
}

bool OrderBook::apply(const L2Tick& t) {
  if (t.symbol != symbol_) return false;

  if (!has_snapshot_) {
    if (t.action != L2Action::Snapshot) return false;
    if (t.qty <= 0) return false;
    const bool ok = (t.side == Side::Bid)
                        ? upsert(bid_, bid_size_, true, t.price, t.qty, false)
                        : upsert(ask_, ask_size_, false, t.price, t.qty, false);
    if (!ok) return false;
    last_seq_ = t.seq;
    has_snapshot_ = true;
    return !crossed();
  }

  if (t.action == L2Action::Snapshot) {
    // 快照行重订: 允许同 seq 多行, seq 回退视为脏数据
    if (t.seq < last_seq_) return false;
    if (t.seq > last_seq_) last_seq_ = t.seq;
    if (t.qty <= 0) return !crossed();
    const bool ok = (t.side == Side::Bid)
                        ? upsert(bid_, bid_size_, true, t.price, t.qty, false)
                        : upsert(ask_, ask_size_, false, t.price, t.qty, false);
    if (!ok) return false;
    return !crossed();
  }

  // 增量必须严格连续, 断层/重复一律返回 false 让上层清书
  if (t.seq != last_seq_ + 1) return false;
  last_seq_ = t.seq;

  bool ok = true;
  if (t.side == Side::Bid) {
    switch (t.action) {
      case L2Action::New:
        ok = upsert(bid_, bid_size_, true, t.price, t.qty, true);
        break;
      case L2Action::Change:
        if (t.qty == 0)
          ok = erase_px(bid_, bid_size_, t.price);
        else
          ok = upsert(bid_, bid_size_, true, t.price, t.qty, false);
        break;
      case L2Action::Delete:
        ok = erase_px(bid_, bid_size_, t.price);
        break;
      case L2Action::Trade:
        ok = consume(bid_, bid_size_, t.price, t.qty);
        break;
      default:
        return false;
    }
  } else {
    switch (t.action) {
      case L2Action::New:
        ok = upsert(ask_, ask_size_, false, t.price, t.qty, true);
        break;
      case L2Action::Change:
        if (t.qty == 0)
          ok = erase_px(ask_, ask_size_, t.price);
        else
          ok = upsert(ask_, ask_size_, false, t.price, t.qty, false);
        break;
      case L2Action::Delete:
        ok = erase_px(ask_, ask_size_, t.price);
        break;
      case L2Action::Trade:
        ok = consume(ask_, ask_size_, t.price, t.qty);
        break;
      default:
        return false;
    }
  }
  if (!ok) return false;  // 容量溢出也走重订路径
  return !crossed();
}

std::optional<PriceTick> OrderBook::best_bid() const {
  if (bid_size_ == 0) return std::nullopt;
  return bid_[0].price;
}

std::optional<PriceTick> OrderBook::best_ask() const {
  if (ask_size_ == 0) return std::nullopt;
  return ask_[0].price;
}

std::optional<int64_t> OrderBook::mid() const {
  auto b = best_bid();
  auto a = best_ask();
  if (!b || !a) return std::nullopt;
  return (*b + *a) / 2;
}

bool OrderBook::crossed() const {
  if (bid_size_ == 0 || ask_size_ == 0) return false;
  return bid_[0].price >= ask_[0].price;
}

std::vector<Level> OrderBook::bids(int n) const {
  std::vector<Level> out;
  const int m = (n < bid_size_) ? n : bid_size_;
  out.reserve(static_cast<std::size_t>(m > 0 ? m : 0));
  for (int i = 0; i < m; ++i) out.push_back(bid_[i]);
  return out;
}

std::vector<Level> OrderBook::asks(int n) const {
  std::vector<Level> out;
  const int m = (n < ask_size_) ? n : ask_size_;
  out.reserve(static_cast<std::size_t>(m > 0 ? m : 0));
  for (int i = 0; i < m; ++i) out.push_back(ask_[i]);
  return out;
}

}  // namespace hft
