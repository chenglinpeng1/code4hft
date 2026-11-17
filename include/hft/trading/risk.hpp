#pragma once
// Risk: 下单前风控, 全部同步无 IO, 热路径只做整数比较
// reject_code: 0=通过 1=数量非法 2=超单笔上限 3=限价非法 4=持仓会超限 5=挂单数超限
#include <cstdint>

#include "hft/common/types.hpp"
#include "hft/trading/order.hpp"

namespace hft {

struct RiskLimits {
  Qty max_order_qty = 10000;  // 单笔上限
  Qty max_position = 50000;   // 净持仓绝对值上限
  int max_open_orders = 128;  // 挂单数上限
};

struct RiskChecker {
  explicit RiskChecker(RiskLimits lim = {}) : lim_(lim) {}

  // prospective_net: 若全成交后的净持仓(调用方算好传进来)
  // open_orders: 当前挂单数
  std::uint32_t check(const Order& o, Qty prospective_net_abs, int open_orders) const {
    if (o.qty <= 0 || o.leaves <= 0) return 1;
    if (o.qty > lim_.max_order_qty) return 2;
    if (o.type == OrderType::Limit && o.price <= 0) return 3;
    if (prospective_net_abs > lim_.max_position) return 4;
    if (open_orders >= lim_.max_open_orders) return 5;
    return 0;
  }

  const RiskLimits& limits() const { return lim_; }

 private:
  RiskLimits lim_;
};

}  // namespace hft
