// feed_pipe: P0-2 收包链路端到端演示
// producer 线程写 32B ITCH-like 消息进 pipe -> 主线程 Poller wait -> 读 -> on_wire -> pump
// 输出 queue_wait / apply 两套 p99, 验证 ts_recv/ts_book 三级打点
#include <cstdio>
#include <cstring>
#include <thread>
#include <unistd.h>
#include <vector>

#include "hft/common/clock.hpp"
#include "hft/marketdata/feed_handler.hpp"
#include "hft/marketdata/itch_parser.hpp"
#include "hft/transport/poller.hpp"

using namespace hft;

int main() {
  // 演示改用直调 on_wire 的方式, 不依赖上面的占位编码器:
  // producer 拼好 L2Tick -> 编成 decode 能读的 32B -> 写 pipe -> 消费侧读出 decode。
  // 为绕开初版 32B qty/ts 复用坑, 这里 producer 直接按 decode 偏移手写 32B:
  // [0]=type [1]=side [4..7]=sym [8..15]=seq [16..23]=price [24..31]=qty, ts=qty。
  int fds[2];
  if (::pipe(fds) != 0) {
    std::printf("pipe failed\n");
    return 1;
  }

  constexpr int kMsgs = 20000;
  std::thread producer([wfd = fds[1]] {
    char buf[kItchMsgLen];
    auto emit = [&](char type, int side, std::uint32_t sym, std::uint64_t seq,
                    std::int64_t px, std::int64_t qty) {
      std::memset(buf, 0, sizeof(buf));
      buf[0] = type;
      buf[1] = (char)side;
      std::memcpy(buf + 4, &sym, 4);
      std::memcpy(buf + 8, &seq, 8);
      std::memcpy(buf + 16, &px, 8);
      std::memcpy(buf + 24, &qty, 8);  // qty 与 ts 共用 8B, ts=qty, 演示够用
      const char* p = buf;
      std::size_t left = sizeof(buf);
      while (left > 0) {
        ssize_t n = ::write(wfd, p, left);
        if (n <= 0) break;
        p += n;
        left -= (std::size_t)n;
      }
    };
    emit('S', 0, 1, 1, 10000, 1000);
    emit('S', 0, 1, 1, 9990, 500);
    emit('S', 1, 1, 1, 10010, 1000);
    emit('S', 1, 1, 1, 10020, 500);
    for (int i = 0; i < kMsgs; ++i) {
      // 70% 同价 Change, 30% New/Delete/Trade 轮换, 与 bench 同分布
      // 注意 side 编码: 0=Bid 配 10000, 1=Ask 配 10010, 写反会造交叉盘口
      const int m = i % 10;
      std::uint64_t seq = (std::uint64_t)(2 + i);
      if (m < 7)
        emit('C', (i % 2 ? 0 : 1), 1, seq, (i % 2 ? 10000 : 10010), 1000 + (i % 16));
      else if (m == 7)
        emit('A', 0, 1, seq, 9980 - (i % 5), 10);
      else if (m == 8)
        emit('C', 0, 1, seq, 9980 - ((i - 1) % 5), 0);  // qty=0 -> Delete
      else
        emit('E', 1, 1, seq, 10010, 1);
    }
    ::close(wfd);
  });

  Poller poller;
  if (!poller.valid() || !poller.add_fd(fds[0])) {
    std::printf("poller init failed\n");
    return 1;
  }

  FeedHandler feed(1, 10, 4096);
  std::vector<char> acc;
  acc.reserve(1 << 20);
  char tmp[65536];
  int idle = 0;
  while (true) {
    int r = poller.wait(100);
    if (r > 0) {
      ssize_t n = ::read(fds[0], tmp, sizeof(tmp));
      if (n > 0) {
        idle = 0;
        acc.insert(acc.end(), tmp, tmp + n);
        // 按 32B 切包: 粘包/半包在这里处理, 满 32B 才 on_wire
        std::size_t off = 0;
        while (acc.size() - off >= kItchMsgLen) {
          feed.on_wire(acc.data() + off, kItchMsgLen);
          off += kItchMsgLen;
        }
        if (off > 0)
          acc.erase(acc.begin(),
                    acc.begin() +
                        static_cast<std::vector<char>::difference_type>(off));
        feed.drain();
      } else if (n == 0) {
        break;  // 写端关闭
      }
    } else if (r == 0) {
      feed.drain();
      if (++idle > 20) break;
    } else {
      break;
    }
  }
  producer.join();
  ::close(fds[0]);
  feed.drain();

  const auto& s = feed.stats();
  auto qw = feed.queue_wait().snapshot();
  auto ap = feed.apply_lat().snapshot();
  std::printf("received=%llu decoded=%llu decode_err=%llu queue_full=%llu "
              "consumed=%llu applied=%llu gaps=%llu resyncs=%llu snapshots=%llu\n",
              (unsigned long long)s.received, (unsigned long long)s.decoded,
              (unsigned long long)s.decode_err, (unsigned long long)s.queue_full,
              (unsigned long long)s.consumed, (unsigned long long)s.applied,
              (unsigned long long)s.gaps, (unsigned long long)s.resyncs,
              (unsigned long long)s.snapshots);
  std::printf("queue_wait p50=%llu p99=%llu p999=%llu max=%llu mean=%.1f (ns)\n",
              (unsigned long long)qw.p50, (unsigned long long)qw.p99,
              (unsigned long long)qw.p999, (unsigned long long)qw.max, qw.mean);
  std::printf("apply      p50=%llu p99=%llu p999=%llu max=%llu mean=%.1f (ns)\n",
              (unsigned long long)ap.p50, (unsigned long long)ap.p99,
              (unsigned long long)ap.p999, (unsigned long long)ap.max, ap.mean);
  auto bb = feed.book().best_bid();
  auto ba = feed.book().best_ask();
  std::printf("best_bid=%lld best_ask=%lld\n", bb ? (long long)*bb : -1,
              ba ? (long long)*ba : -1);
  return (s.applied > 0 && s.gaps == 0) ? 0 : 2;
}
