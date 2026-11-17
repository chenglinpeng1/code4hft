#pragma once
// ITCH-like 二进制解析: 外资岗点名的 FIX/OUCH/ITCH 编解码体系先从这里练
// 格式(小端, 定长 32B, 对齐缓存行一半, 好做 replay/mmap):
//   [u8 type][u8 side][u16 pad][u32 symbol][u64 seq][i64 price][i64 qty][u64 ts]
// type: 'A'=Add 'C'=Cancel/Change 'E'=Execute(Trade) 'S'=Snapshot行
// 这不是交易所真实格式, 是练手格式; 换真实上交所/CME 只需换这一层。
#include <cstddef>
#include <cstdint>
#include <optional>

#include "hft/marketdata/tick.hpp"

namespace hft {

inline constexpr std::size_t kItchMsgLen = 32;

struct ItchRaw {
  char type;  // A/C/E/S
  Side side;
  SymbolId symbol;
  SeqNo seq;
  PriceTick price;
  Qty qty;
  Nanos ts;
};

// 定长解析, 返回 nullopt = 长度不够/类型非法, 热路径无异常无分配
inline std::optional<ItchRaw> decode_itch(const char* p, std::size_t n) {
  if (n < kItchMsgLen) return std::nullopt;
  ItchRaw m{};
  m.type = p[0];
  m.side = (p[1] == 0) ? Side::Bid : Side::Ask;
  // p[2..3] pad
  const auto* u32 = reinterpret_cast<const std::uint32_t*>(p + 4);
  const auto* u64a = reinterpret_cast<const std::uint64_t*>(p + 8);
  const auto* i64a = reinterpret_cast<const std::int64_t*>(p + 16);
  const auto* u64b = reinterpret_cast<const std::uint64_t*>(p + 24);
  m.symbol = u32[0];
  m.seq = u64a[0];
  m.price = i64a[0];
  m.qty = i64a[1];
  m.ts = u64b[0];
  if (m.type != 'A' && m.type != 'C' && m.type != 'E' && m.type != 'S')
    return std::nullopt;
  return m;
}

inline L2Tick to_tick(const ItchRaw& m) {
  L2Tick t{};
  t.symbol = m.symbol;
  t.seq = m.seq;
  t.side = m.side;
  t.price = m.price;
  t.qty = m.qty;
  t.ts_exchange = m.ts;
  switch (m.type) {
    case 'S': t.action = L2Action::Snapshot; break;
    case 'A': t.action = L2Action::New; break;
    case 'C': t.action = (m.qty == 0) ? L2Action::Delete : L2Action::Change; break;
    case 'E': t.action = L2Action::Trade; break;
    default: t.action = L2Action::New; break;
  }
  return t;
}

}  // namespace hft
