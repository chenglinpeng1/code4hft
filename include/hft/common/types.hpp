#pragma once
#include <cstdint>

namespace hft {

// 价格用定点 int64: price_tick = px * 1e4, 避免 double 做撮合键
using PriceTick = std::int64_t;
using Qty = std::int64_t;
using SeqNo = std::uint64_t;
using SymbolId = std::uint32_t;
// 全链路纳秒时间戳: ts_exchange -> ts_recv -> ts_book
using Nanos = std::uint64_t;

enum class Side : std::uint8_t { Bid = 0, Ask = 1 };
enum class L2Action : std::uint8_t {
  Snapshot = 0,  // 整幅快照行
  New = 1,       // 新增一档/加量
  Change = 2,    // 改量
  Delete = 3,    // 删档
  Trade = 4      // 成交(只吃量,不挂单)
};

inline const char* to_string(Side s) { return s == Side::Bid ? "BID" : "ASK"; }

}  // namespace hft
