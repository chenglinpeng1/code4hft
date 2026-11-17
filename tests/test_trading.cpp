// test_trading: 模拟撮合 + Order 生命周期 + 风控 + 持仓全链路
#include <cstdio>
#include <vector>

#include "hft/trading/matcher.hpp"

#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) {                                                        \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);         \
      return 1;                                                        \
    }                                                                  \
  } while (0)

using namespace hft;

static void snap4(SimEngine& e, SeqNo seq = 1) {
  L2Tick s[4] = {};
  s[0] = {.symbol = 1, .seq = seq, .side = Side::Bid, .action = L2Action::Snapshot, .price = 10000, .qty = 1000};
  s[1] = {.symbol = 1, .seq = seq, .side = Side::Bid, .action = L2Action::Snapshot, .price = 9990, .qty = 500};
  s[2] = {.symbol = 1, .seq = seq, .side = Side::Ask, .action = L2Action::Snapshot, .price = 10010, .qty = 1000};
  s[3] = {.symbol = 1, .seq = seq, .side = Side::Ask, .action = L2Action::Snapshot, .price = 10020, .qty = 500};
  e.on_snapshot(s, 4);
}

int main() {
  // 1) 穿价限价买 -> 即时全成交
  {
    SimEngine e(1);
    snap4(e);
    std::vector<Fill> fs;
    Order o{.id = 101, .side = Side::Bid, .type = OrderType::Limit, .price = 10010, .qty = 100};
    Order* r = e.new_order(o, fs);
    CHECK(r && r->status == OrderStatus::Filled);
    CHECK(fs.size() == 1 && fs[0].price == 10010 && fs[0].qty == 100);
    CHECK(e.position().net == 100 && e.position().avg_px == 10010);
    CHECK(e.open_orders() == 0);
  }

  // 2) 不穿价限价买 -> 挂起, 行情下移后被动碰单
  {
    SimEngine e(1);
    snap4(e);
    std::vector<Fill> fs;
    Order o{.id = 1, .side = Side::Bid, .type = OrderType::Limit, .price = 9990, .qty = 50};
    Order* r = e.new_order(o, fs);
    CHECK(r->status == OrderStatus::Resting && fs.empty());
    CHECK(e.open_orders() == 1);
    // ask 降到 9990 -> resting 被碰
    L2Tick t{.symbol = 1, .seq = 2, .side = Side::Ask, .action = L2Action::Change, .price = 10010, .qty = 0};
    (void)t;
    // 直接换一整本: ask 变成 9990/10000
    L2Tick s2[2] = {};
    s2[0] = {.symbol = 1, .seq = 2, .side = Side::Bid, .action = L2Action::Snapshot, .price = 9980, .qty = 100};
    s2[1] = {.symbol = 1, .seq = 2, .side = Side::Ask, .action = L2Action::Snapshot, .price = 9990, .qty = 100};
    e.on_snapshot(s2, 2);
    CHECK(e.find(1)->status == OrderStatus::Filled);
    CHECK(e.position().net == 50 && e.open_orders() == 0);
  }

  // 3) 市价扫多档 + 残留撤掉(IOC)
  {
    SimEngine e(1);
    snap4(e);
    std::vector<Fill> fs;
    Order o{.id = 7, .side = Side::Bid, .type = OrderType::Market, .qty = 1200};
    Order* r = e.new_order(o, fs);
    CHECK(fs.size() == 2);  // 1000@10010 + 200@10020
    CHECK(fs[0].price == 10010 && fs[0].qty == 1000);
    CHECK(fs[1].price == 10020 && fs[1].qty == 200);
    CHECK(r->status == OrderStatus::Filled && e.position().net == 1200);
  }
  {
    SimEngine e(1);
    snap4(e);
    std::vector<Fill> fs;
    Order o{.id = 8, .side = Side::Bid, .type = OrderType::Market, .qty = 5000};
    Order* r = e.new_order(o, fs);  // 对边只有 1500
    CHECK(fs.size() == 2 && r->status == OrderStatus::Cancelled);
    CHECK(r->filled == 1500 && r->leaves == 3500);
  }

  // 4) 撤单
  {
    SimEngine e(1);
    snap4(e);
    std::vector<Fill> fs;
    Order o{.id = 9, .side = Side::Ask, .type = OrderType::Limit, .price = 10020, .qty = 10};
    e.new_order(o, fs);
    CHECK(e.open_orders() == 1);
    CHECK(e.cancel(9) && e.find(9)->status == OrderStatus::Cancelled);
    CHECK(e.open_orders() == 0);
    CHECK(!e.cancel(9));            // 已终态再撤失败
    CHECK(!e.cancel(9999));          // 不存在
  }

  // 5) 风控拒绝: 数量非法/超单笔/限价非法/持仓超限/挂单超限
  {
    SimEngine e(1, RiskLimits{.max_order_qty = 100, .max_position = 200, .max_open_orders = 1});
    snap4(e);
    std::vector<Fill> fs;
    Order bad{.id = 21, .side = Side::Bid, .type = OrderType::Limit, .price = 10010, .qty = 0};
    CHECK(e.new_order(bad, fs)->status == OrderStatus::Rejected);
    CHECK(e.find(21)->reject_code == 1);
    Order big{.id = 22, .side = Side::Bid, .type = OrderType::Limit, .price = 10010, .qty = 101};
    CHECK(e.new_order(big, fs)->reject_code == 2);
    Order nopx{.id = 23, .side = Side::Bid, .type = OrderType::Limit, .price = 0, .qty = 10};
    CHECK(e.new_order(nopx, fs)->reject_code == 3);
    Order toolarge{.id = 24, .side = Side::Bid, .type = OrderType::Limit, .price = 10010, .qty = 100};
    e.new_order(toolarge, fs);  // net=100
    // 挂单占位场景(独立引擎, max_open_orders=1):
    SimEngine e2(1, RiskLimits{.max_order_qty = 1000, .max_position = 100000, .max_open_orders = 1});
    snap4(e2);
    Order r1{.id = 31, .side = Side::Bid, .type = OrderType::Limit, .price = 9000, .qty = 10};
    Order r2{.id = 32, .side = Side::Bid, .type = OrderType::Limit, .price = 9001, .qty = 10};
    e2.new_order(r1, fs);
    CHECK(e2.new_order(r2, fs)->reject_code == 5);
    // 持仓超限: max_position=200, 已有 net=100, 再买 101 -> prospective 201
    // (qty=101 同时超单笔 100, 先报 2; 用 qty=100 且 net 再加一点另测 4)
    Order over2{.id = 26, .side = Side::Bid, .type = OrderType::Limit, .price = 10010, .qty = 101};
    CHECK(e.new_order(over2, fs)->reject_code == 2);
    SimEngine e3(1, RiskLimits{.max_order_qty = 1000, .max_position = 150, .max_open_orders = 64});
    snap4(e3);
    Order c1{.id = 51, .side = Side::Bid, .type = OrderType::Limit, .price = 10010, .qty = 100};
    e3.new_order(c1, fs);  // net=100
    Order c2{.id = 52, .side = Side::Bid, .type = OrderType::Limit, .price = 10010, .qty = 100};
    e3.new_order(c2, fs);  // prospective=200 > 150 -> 4 (限价穿价会成交, 但风控先拦)
    CHECK(e3.find(52)->reject_code == 4);
  }

  // 6) 持仓: 加权均价 + 反向平仓实现盈亏 + 反手
  {
    SimEngine e(1);
    snap4(e);
    std::vector<Fill> fs;
    Order b1{.id = 41, .side = Side::Bid, .type = OrderType::Limit, .price = 10010, .qty = 100};
    e.new_order(b1, fs);  // 100@10010
    Order b2{.id = 42, .side = Side::Bid, .type = OrderType::Limit, .price = 10020, .qty = 100};
    e.new_order(b2, fs);  // 100@10010(对边最优仍是10010)
    CHECK(e.position().net == 200 && e.position().avg_px == 10010);
    Order s1{.id = 43, .side = Side::Ask, .type = OrderType::Limit, .price = 9990, .qty = 150};
    e.new_order(s1, fs);  // 卖 150@10000(对边最优 bid)
    CHECK(e.position().net == 50);
    CHECK(e.position().realized == 150 * (10000 - 10010));  // -1500
    Order s2{.id = 44, .side = Side::Ask, .type = OrderType::Limit, .price = 9900, .qty = 100};
    e.new_order(s2, fs);  // 先平剩余 50 再反手空 50
    CHECK(e.position().net == -50);
    CHECK(e.position().avg_px == 10000);  // 反手按成交价重建
  }

  // 7) on_tick 断层透传 false, book 语义不变
  {
    SimEngine e(1);
    snap4(e, 10);
    L2Tick bad{.symbol = 1, .seq = 99, .side = Side::Bid, .action = L2Action::New, .price = 9995, .qty = 10};
    CHECK(!e.on_tick(bad));
    L2Tick good{.symbol = 1, .seq = 11, .side = Side::Bid, .action = L2Action::New, .price = 9995, .qty = 10};
    CHECK(e.on_tick(good));
  }

  std::printf("test_trading PASS\n");
  return 0;
}
