#pragma once
// Level2 行情增量: 快照行 + 逐笔 delta 的统一表示
// 上海期权/期货 L2 一般就是这个语义: 整幅 + 增量, 丢包靠 seq 发现
#include "hft/common/types.hpp"

namespace hft {

struct L2Tick {
  SymbolId symbol = 0;
  SeqNo seq = 0;            // 交易所序号, 断层=丢包, 触发全量重订
  Side side = Side::Bid;
  L2Action action = L2Action::New;
  PriceTick price = 0;      // 定点价
  Qty qty = 0;              // 档位总量(Delete/Trade 时语义见 order_book)
  Nanos ts_exchange = 0;    // 交易所时间( wire ts, 与本地时钟不同源, 跨机对比仅参考 )
  Nanos ts_recv = 0;        // 收包线程 decode 后打点, 进队列前
  Nanos ts_book = 0;        // 重建线程 pop 后打点, apply 前(消费者回填, 不上队列)
};

}  // namespace hft
