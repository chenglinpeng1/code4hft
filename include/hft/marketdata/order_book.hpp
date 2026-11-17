#pragma once
// OrderBook: L2 快照 + 增量重建, 盘口重建是量化第二步
// 设计取舍(面试可直接说):
//   - P0-1 已数组化: 双边定长排序数组(线性查找+memmove), 10 档下全在
//     1~2 个 cacheline, 替代 std::map 的 logN + 跳指针;
//   - apply() 返回 false = 序列断层/脏数据, 上层必须清书重订快照;
//   - Trade 只吃对边量, 不挂单, 避免把成交当报价污染 mid。
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "hft/common/types.hpp"
#include "hft/marketdata/tick.hpp"

namespace hft {

struct Level {
  PriceTick price = 0;
  Qty qty = 0;
};

class OrderBook {
 public:
  // kMaxLevels: 单边容量上限, 超出则丢最差价(并计数, 实盘告警重订)。
  // depth_: 对外默认档数(快照查询/风控用), 不截断存储, 只约束查询语义。
  static constexpr int kMaxLevels = 64;

  explicit OrderBook(SymbolId symbol, int depth = 10);

  void clear();
  // 整幅快照: 先 clear 再逐行 Snapshot
  void apply_snapshot(const L2Tick* ticks, std::size_t n);
  // 单笔增量, 返回 false 表示 seq 断层/交叉盘口/容量溢出, 需重订快照
  bool apply(const L2Tick& t);

  std::optional<PriceTick> best_bid() const;
  std::optional<PriceTick> best_ask() const;
  std::optional<int64_t> mid() const;  // 定点 mid, 精度 0.5 tick
  bool crossed() const;

  std::vector<Level> bids(int n) const;
  std::vector<Level> asks(int n) const;
  SeqNo last_seq() const { return last_seq_; }
  SymbolId symbol() const { return symbol_; }
  int depth() const { return depth_; }
  int bid_size() const { return bid_size_; }
  int ask_size() const { return ask_size_; }
  std::uint64_t drops() const { return drops_; }

 private:
  // side=true: bid 降序; side=false: ask 升序。线性查找对 10 档最快,
  // 且全数组常驻 L1, 无指针追逐、无分配, 分支可预测。
  int find_level(const Level* a, int n, PriceTick px) const;
  int lower_bound_desc(const Level* a, int n, PriceTick px) const;  // bid 插入位
  int lower_bound_asc(const Level* a, int n, PriceTick px) const;   // ask 插入位
  void erase_at(Level* a, int& n, int idx);
  // upsert: 存在则按 mode 更新, 不存在则排序插入。返回 false=容量满。
  bool upsert(Level* a, int& n, bool is_bid, PriceTick px, Qty qty, bool add);
  bool erase_px(Level* a, int& n, PriceTick px);
  bool consume(Level* a, int& n, PriceTick px, Qty qty);

  SymbolId symbol_;
  int depth_;
  SeqNo last_seq_ = 0;
  bool has_snapshot_ = false;
  std::uint64_t drops_ = 0;  // 容量溢出丢弃计数, 实盘接告警
  Level bid_[kMaxLevels]{};
  Level ask_[kMaxLevels]{};
  int bid_size_ = 0;
  int ask_size_ = 0;
};

}  // namespace hft
