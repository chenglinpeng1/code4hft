#pragma once
// Order: 订单生命周期最小闭环
// New -> Accepted/Resting/Filled (+ Rejected/Cancelled)
// 对标外资岗: 订单管理 / 风控 / 全球分布式(先单市场单线程正确)
#include <cstdint>

#include "hft/common/types.hpp"

namespace hft {

using OrderId = std::uint64_t;

enum class OrderType : std::uint8_t { Limit = 0, Market = 1 };

enum class OrderStatus : std::uint8_t {
  New = 0,       // 刚创建, 未进风控
  Accepted = 1,  // 风控通过(瞬态, 随即转 Resting/Filled)
  Rejected = 2,  // 风控拒绝, 终态
  Resting = 3,   // 挂在本地账本等市场来碰
  Partial = 4,   // 部分成交后剩余继续挂
  Filled = 5,    // 终态
  Cancelled = 6  // 终态
};

inline const char* to_string(OrderStatus s) {
  switch (s) {
    case OrderStatus::New: return "New";
    case OrderStatus::Accepted: return "Accepted";
    case OrderStatus::Rejected: return "Rejected";
    case OrderStatus::Resting: return "Resting";
    case OrderStatus::Partial: return "Partial";
    case OrderStatus::Filled: return "Filled";
    case OrderStatus::Cancelled: return "Cancelled";
    default: return "?";
  }
}

struct Order {
  OrderId id = 0;
  SymbolId symbol = 0;
  Side side = Side::Bid;  // Bid=买, Ask=卖(复用行情 Side, 别名 Buy/Sell)
  OrderType type = OrderType::Limit;
  PriceTick price = 0;  // 市价单忽略
  Qty qty = 0;          // 原始量
  Qty leaves = 0;       // 剩余量
  Qty filled = 0;       // 累计成交
  OrderStatus status = OrderStatus::New;
  std::uint32_t reject_code = 0;  // 0=无, 见 risk.hpp
  Nanos ts_in = 0;                // 下单本地时间
};

struct Fill {
  OrderId order_id = 0;
  SymbolId symbol = 0;
  Side side = Side::Bid;
  PriceTick price = 0;
  Qty qty = 0;
  Nanos ts_fill = 0;
};

}  // namespace hft
