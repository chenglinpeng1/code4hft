#pragma once
// Position: 单标的净持仓 + 均价 + 已实现盈亏(价格*数量定点单位)
// 均价法: 同向加仓加权, 反向先平仓实现盈亏, 反手后剩余按成交价重建均价
#include <cstdint>

#include "hft/common/types.hpp"
#include "hft/trading/order.hpp"

namespace hft {

struct Position {
  SymbolId symbol = 0;
  Qty net = 0;               // 净持仓, 买为正卖为负
  PriceTick avg_px = 0;      // net!=0 时有效
  std::int64_t realized = 0;  // 已实现 pnl(定点 px*qty)
  std::int64_t turnover = 0;  // 累计成交额 px*qty(绝对值和)
  std::uint64_t fills = 0;

  void apply_fill(const Fill& f) {
    const Qty delta = (f.side == Side::Bid) ? f.qty : -f.qty;
    turnover += f.price * f.qty;
    ++fills;
    if (net == 0) {
      net = delta;
      avg_px = f.price;
      return;
    }
    const bool same_dir = (net > 0 && delta > 0) || (net < 0 && delta < 0);
    if (same_dir) {
      const Qty an = (net > 0 ? net : -net);
      const Qty ad = (delta > 0 ? delta : -delta);
      avg_px = (avg_px * an + f.price * ad) / (an + ad);
      net += delta;
      return;
    }
    // 反向: 先平仓
    const Qty an = (net > 0 ? net : -net);
    const Qty ad = (delta > 0 ? delta : -delta);
    const Qty closed = (an < ad) ? an : ad;
    if (net > 0)
      realized += closed * (f.price - avg_px);
    else
      realized += closed * (avg_px - f.price);
    net += delta;
    if (net == 0) {
      avg_px = 0;
    } else if ((net > 0) != (net - delta > 0)) {
      // 反手: 剩余按当前成交价建仓
      avg_px = f.price;
    }
  }
};

}  // namespace hft
