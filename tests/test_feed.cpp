// test_feed: FeedHandler 收包->队列->重建链路回归
// 常开 CHECK, Release 下依然有效
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#include "hft/marketdata/feed_handler.hpp"
#include "hft/transport/poller.hpp"

#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) {                                                        \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);         \
      return 1;                                                        \
    }                                                                  \
  } while (0)

using namespace hft;

// 按 decode_itch 偏移手写 32B: [0]=type [1]=side [4]=sym [8]=seq [16]=px [24]=qty
// 注意初版格式 qty/ts 共用 8B, ts_exchange==qty, 演示够用, P1 改 40B 格式。
static void emit(char* buf, char type, int side, std::uint32_t sym,
                 std::uint64_t seq, std::int64_t px, std::int64_t qty) {
  std::memset(buf, 0, kItchMsgLen);
  buf[0] = type;
  buf[1] = (char)side;
  std::memcpy(buf + 4, &sym, 4);
  std::memcpy(buf + 8, &seq, 8);
  std::memcpy(buf + 16, &px, 8);
  std::memcpy(buf + 24, &qty, 8);
}

int main() {
  char buf[kItchMsgLen];

  // 1) 快照装配 + 增量 apply
  {
    FeedHandler f(1, 10, 64);
    emit(buf, 'S', 0, 1, 1, 10000, 100);
    CHECK(f.on_wire(buf, kItchMsgLen, 1000));
    emit(buf, 'S', 1, 1, 1, 10010, 100);
    CHECK(f.on_wire(buf, kItchMsgLen, 2000));
    emit(buf, 'C', 0, 1, 2, 10000, 150);
    CHECK(f.on_wire(buf, kItchMsgLen, 3000));
    CHECK(f.drain() == 3);
    const auto& s = f.stats();
    CHECK(s.decoded == 3 && s.applied == 1 && s.snapshots == 1 && s.gaps == 0);
    auto bb = f.book().best_bid();
    CHECK(bb && *bb == 10000 && f.book().bids(1)[0].qty == 150);
    CHECK(f.queue_wait().size() == 3);  // 每个 pop 都记 queue_wait
    CHECK(f.apply_lat().size() == 1);   // 只有首个增量走 apply
  }

  // 2) 非法类型 -> decode_err
  {
    FeedHandler f(1, 10, 64);
    emit(buf, 'X', 0, 1, 1, 10000, 100);
    CHECK(!f.on_wire(buf, kItchMsgLen, 1000));
    CHECK(f.stats().decode_err == 1 && f.stats().decoded == 0);
  }

  // 3) 队列背压: 容量 1 的队列(cap 规整到 >=1 的 2 的幂), 第二包进不去
  {
    FeedHandler f(1, 10, 1);
    emit(buf, 'S', 0, 1, 1, 10000, 100);
    CHECK(f.on_wire(buf, kItchMsgLen, 1000));
    emit(buf, 'S', 1, 1, 1, 10010, 100);
    CHECK(!f.on_wire(buf, kItchMsgLen, 2000));  // 满
    CHECK(f.stats().queue_full == 1);
  }

  // 4) seq 断层 -> gaps+resync, 下一轮快照可恢复
  {
    FeedHandler f(1, 10, 64);
    emit(buf, 'S', 0, 1, 1, 10000, 100);
    CHECK(f.on_wire(buf, kItchMsgLen, 1000));
    emit(buf, 'S', 1, 1, 1, 10010, 100);
    CHECK(f.on_wire(buf, kItchMsgLen, 2000));
    emit(buf, 'C', 0, 1, 2, 10000, 150);
    CHECK(f.on_wire(buf, kItchMsgLen, 3000));
    emit(buf, 'C', 0, 1, 99, 10000, 200);  // 跳 seq
    CHECK(f.on_wire(buf, kItchMsgLen, 4000));
    f.drain();
    CHECK(f.stats().gaps == 1 && f.stats().resyncs == 1);
    // 重订: 新一轮快照 + 增量恢复
    emit(buf, 'S', 0, 1, 100, 10000, 300);
    CHECK(f.on_wire(buf, kItchMsgLen, 5000));
    emit(buf, 'S', 1, 1, 100, 10010, 300);
    CHECK(f.on_wire(buf, kItchMsgLen, 6000));
    emit(buf, 'C', 0, 1, 101, 10000, 310);
    CHECK(f.on_wire(buf, kItchMsgLen, 7000));
    f.drain();
    CHECK(f.stats().snapshots == 2);
    CHECK(f.book().bids(1)[0].qty == 310);
  }

  // 5) Poller: pipe 可读事件(Linux 走 epoll, macOS 走 poll 兜底)
  {
    int fds[2];
    CHECK(::pipe(fds) == 0);
    Poller p;
    CHECK(p.valid() && p.add_fd(fds[0]));
    CHECK(p.wait(0) == 0);  // 无数据超时
    CHECK(::write(fds[1], "x", 1) == 1);
    CHECK(p.wait(1000) > 0);  // 有数据可读
    ::close(fds[0]);
    ::close(fds[1]);
  }

  std::printf("test_feed PASS\n");
  return 0;
}
