// 无 gtest 依赖的最小断言集: 编译即跑, 对应 CMake add_test
// 注意: Release 会定义 NDEBUG 导致 <cassert> 的 assert 被禁用,
// 所以这里用常开的 CHECK, 保证 Release 下仍真实校验。
#include <cstdio>

#include "hft/marketdata/order_book.hpp"

#define CHECK(c)                                                              \
  do {                                                                        \
    if (!(c)) {                                                               \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                \
      return 1;                                                               \
    }                                                                         \
  } while (0)

using namespace hft;

int main() {
  OrderBook book(1, 10);
  // 1) 整幅快照
  L2Tick snap[4] = {};
  snap[0] = {.symbol = 1, .seq = 100, .side = Side::Bid, .action = L2Action::Snapshot, .price = 10000, .qty = 50};
  snap[1] = {.symbol = 1, .seq = 100, .side = Side::Bid, .action = L2Action::Snapshot, .price = 9990, .qty = 30};
  snap[2] = {.symbol = 1, .seq = 100, .side = Side::Ask, .action = L2Action::Snapshot, .price = 10010, .qty = 40};
  snap[3] = {.symbol = 1, .seq = 100, .side = Side::Ask, .action = L2Action::Snapshot, .price = 10020, .qty = 20};
  book.apply_snapshot(snap, 4);
  CHECK(book.last_seq() == 100);
  CHECK(book.best_bid() && *book.best_bid() == 10000);
  CHECK(book.best_ask() && *book.best_ask() == 10010);
  CHECK(book.mid() && *book.mid() == 10005);

  // 2) 增量 New/Change/Delete
  CHECK(book.apply({.symbol = 1, .seq = 101, .side = Side::Bid, .action = L2Action::New, .price = 9995, .qty = 10}));
  CHECK(book.bids(10).size() == 3);
  CHECK(book.apply({.symbol = 1, .seq = 102, .side = Side::Bid, .action = L2Action::Change, .price = 9995, .qty = 25}));
  CHECK(book.apply({.symbol = 1, .seq = 103, .side = Side::Bid, .action = L2Action::Delete, .price = 9995}));
  CHECK(book.bids(10).size() == 2);

  // 3) Trade 只吃量
  CHECK(book.apply({.symbol = 1, .seq = 104, .side = Side::Ask, .action = L2Action::Trade, .price = 10010, .qty = 10}));
  auto asks = book.asks(10);
  CHECK(asks[0].qty == 30);

  // 4) seq 断层必须返回 false
  CHECK(!book.apply({.symbol = 1, .seq = 106, .side = Side::Bid, .action = L2Action::New, .price = 9995, .qty = 1}));

  // 5) 交叉盘口必须返回 false(脏数据)
  OrderBook b2(7, 5);
  L2Tick s2[2] = {};
  s2[0] = {.symbol = 7, .seq = 1, .side = Side::Bid, .action = L2Action::Snapshot, .price = 100, .qty = 10};
  s2[1] = {.symbol = 7, .seq = 1, .side = Side::Ask, .action = L2Action::Snapshot, .price = 110, .qty = 10};
  b2.apply_snapshot(s2, 2);
  CHECK(!b2.apply({.symbol = 7, .seq = 2, .side = Side::Bid, .action = L2Action::New, .price = 120, .qty = 5}));

  // 6) 无快照直接增量必须拒绝
  OrderBook b3(9, 5);
  CHECK(!b3.apply({.symbol = 9, .seq = 1, .side = Side::Bid, .action = L2Action::New, .price = 1, .qty = 1}));

  // 7) 快照乱序输入仍有序(数组化回归: bid 降序 / ask 升序)
  OrderBook b4(11, 10);
  L2Tick s4[4] = {};
  s4[0] = {.symbol = 11, .seq = 5, .side = Side::Ask, .action = L2Action::Snapshot, .price = 10020, .qty = 5};
  s4[1] = {.symbol = 11, .seq = 5, .side = Side::Bid, .action = L2Action::Snapshot, .price = 9990, .qty = 5};
  s4[2] = {.symbol = 11, .seq = 5, .side = Side::Ask, .action = L2Action::Snapshot, .price = 10010, .qty = 5};
  s4[3] = {.symbol = 11, .seq = 5, .side = Side::Bid, .action = L2Action::Snapshot, .price = 10000, .qty = 5};
  b4.apply_snapshot(s4, 4);
  auto bb4 = b4.bids(10);
  auto ba4 = b4.asks(10);
  CHECK(bb4.size() == 2 && bb4[0].price == 10000 && bb4[1].price == 9990);
  CHECK(ba4.size() == 2 && ba4[0].price == 10010 && ba4[1].price == 10020);

  // 8) 同价 New 做加量(非覆盖)
  CHECK(b4.apply({.symbol = 11, .seq = 6, .side = Side::Bid, .action = L2Action::New, .price = 10000, .qty = 7}));
  CHECK(b4.bids(10)[0].qty == 12);

  // 9) 容量溢出走重订路径: 填满 64 档后第 65 档返回 false + drops 计数
  OrderBook b5(12, 64);
  L2Tick base[2] = {};
  base[0] = {.symbol = 12, .seq = 1, .side = Side::Bid, .action = L2Action::Snapshot, .price = 1000, .qty = 1};
  base[1] = {.symbol = 12, .seq = 1, .side = Side::Ask, .action = L2Action::Snapshot, .price = 2000, .qty = 1};
  b5.apply_snapshot(base, 2);
  SeqNo sq = 2;
  for (int i = 1; i < OrderBook::kMaxLevels; ++i) {
    // bid 侧从 999 向下填, ask 侧已占 1 档所以 bid 只能再填 63 档即满 64
    bool ok = b5.apply({.symbol = 12, .seq = sq++, .side = Side::Bid,
                        .action = L2Action::New, .price = 1000 - i, .qty = 1});
    if (i < 63) CHECK(ok);
    else if (i == 63) CHECK(ok && b5.bid_size() == 64);
  }
  CHECK(b5.bid_size() == 64);
  CHECK(!b5.apply({.symbol = 12, .seq = sq++, .side = Side::Bid,
                    .action = L2Action::New, .price = 1, .qty = 1}));
  CHECK(b5.drops() == 1);

  std::printf("test_orderbook PASS\n");
  return 0;
}
