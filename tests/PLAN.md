# code4hft 方案文档（活文档，随迭代更新）

> 位置：`code4hft/tests/PLAN.md`
> 目标：对标上海高频三岗（50-80K 国内期权 / 70-100K 外资行情交易 / 80-110K 资深 C++），从行情 + OrderBook 起手，用 C++20 搭低延迟骨架。
> 标准：C++20 + CMake + Release -O3，价格定点 `int64`，全链路纳秒打点，p50/p99/p999 可量化。
> 状态（2026-11-16 晚）：P0-1 数组化 + P0-2 收包链路 + P1 交易闭环 + 性能回归门禁已落地，`ctest` 四绿，`perf_gate` 通过。

## 1. 岗位分析结论

三岗重合度 90%，指向同一套技能树，排序如下：

| # | 岗位速写 | 薪资 | 本质定位 |
|---|---|---|---|
| 1 | C++ 高频/海外策略支持/国内期权，私募，北京优先其次上海 | 50-80K * 16-18，年包 ~130-140 万 | 要干活的人：亚太/国内期权实盘链路 + 策略支持，年龄可放宽，门槛相对最低 |
| 3 | 外资 C++ 量化（交易/行情），小型基金，上海徐汇 | 70-100K * 15 | 要行情+交易全栈：Level2/OrderBook 重建 + FIX/OUCH/ITCH 接沪深/港交所/CME/NASDAQ，经验不限，有 junior/senior 两个 HC |
| 2 | 资深 C++ 专家（不限方向，做核心交易），大型金融集团陆家嘴 | 80-110K * 16，年包 ~170-280 万 | 要扛路的人：P7-9，不限交易出身，但要主导过一套生产级高性能组件，对时延/吞吐/稳定性负全责 |

结论：1 是入口，3 是外资标准件，2 是天花板。2 号 JD 把考点全写明白了，照着练即可。

## 2. 三岗共同技能树（= 本项目路线图）

1. 现代 C++：11/14/17 精通，熟悉 20 更佳，RAII/模板/移动语义/对象生命周期/异常安全。
2. 内存模型 + 并发：`std::atomic`、`memory_order`、无锁数据结构、伪共享规避，且能说清“什么场景下会失效”。
3. Linux 底层：线程调度、分配器、IO 栈、`page cache`、`perf/gdb/tcpdump/strace/eBPF`，无日志能定位。
4. 量化认知：`cache miss`/分支预测/TLB/系统调用/上下文切换/NUMA，全部用实测数据说话。
5. 行情 + 交易：Level2/OrderBook 重建、FIX/OUCH/ITCH、二进制编解码、订单网关/行情接入/撮合周边。
6. 性能体系：p50/p99/p999、T0 监控、火焰图、回归机制，可量化可回溯。
7. IO 与网络：`epoll`、`io_uring` 或内核旁路，高吞吐低抖动收发 + 持久化。
8. 稳定性：7x24、多市场、容量规划、压测、故障演练、灰度回滚，重大线上问题牵头到根因。

2 号岗职责 1-6 就是标准答案：方案主导 → 低延迟攻坚（网卡到应用全链路，无锁队列/内存池/绑核/NUMA/缓存友好）→ 性能体系 → IO 架构 → 稳定性兜底 → 跨团队引领。

## 3. 当前骨架（已落地）

选型：`C++20 + CMake + 行情/OrderBook` 先行。

```
code4hft/
  CMakeLists.txt
  include/hft/common/types.hpp clock.hpp cacheline.hpp
  include/hft/core/spsc_queue.hpp object_pool.hpp latency.hpp
  include/hft/marketdata/tick.hpp order_book.hpp itch_parser.hpp feed_handler.hpp
  include/hft/transport/poller.hpp
  include/hft/trading/order.hpp risk.hpp position.hpp matcher.hpp
  src/marketdata/order_book.cpp feed_handler.cpp
  src/trading/matcher.cpp
  src/core/latency.cpp
  tools/replay.cpp bench_orderbook.cpp feed_pipe.cpp
  scripts/perf_gate.sh
  tests/test_orderbook.cpp test_queue.cpp test_feed.cpp test_trading.cpp
  tests/PLAN.md                      # 本文档
```

### 3.1 模块说明

- `common/types.hpp`：`PriceTick(int64 定点, px*1e4)` / `Qty` / `SeqNo` / `SymbolId` / `Nanos` / `Side` / `L2Action(Snapshot/New/Change/Delete/Trade)`。价格不用 `double` 做键，避免撮合误差。
- `common/cacheline.hpp`：`kCacheLine=64`，`HFT_CACHELINE_ALIGNED`，头尾指针隔离防伪共享。
- `common/clock.hpp`：`now_ns()`（steady_clock）+ x86 `rdtsc()`。三级时间戳预留：`ts_exchange/ts_recv/ts_book`。
- `core/spsc_queue.hpp`：SPSC 环形队列，容量 2 的幂，`push-release/pop-acquire`，满/空返回 `false` 做背压，无锁无系统调用，值类型要求 `trivially copyable`。对应链路：收包 → 重建线程。
- `core/object_pool.hpp`：定长对象池，热路径零 `malloc/free`，空池返回 `nullptr` 即背压信号。
- `core/latency.hpp/cpp`：`LatencyRecorder`，热路径只 `record()`，离线 `snapshot()` 排序求 p50/p99/p999/max/mean。
- `marketdata/tick.hpp`：`L2Tick{symbol,seq,side,action,price,qty,ts_exchange,ts_recv,ts_book}`，上海期权/期货 L2 语义：整幅 + 增量，丢包靠 `seq` 发现。三级时间戳：`ts_exchange`（wire，不与本地同源仅参考）→ `ts_recv`（`on_wire`打点进队列前）→ `ts_book`（`pump`弹出后回填不进队列）。
- `marketdata/order_book.hpp/cpp`：`OrderBook(symbol, depth)`，`apply_snapshot()` + `apply()`。
  - 不变量：增量须严格 `seq == last_seq+1`，否则返回 `false`；无快照时直接增量拒绝；交叉盘口（`best_bid >= best_ask`）返回 `false`；`Trade` 只吃对边量不挂单；容量满（单边 64 档）返回 `false` + `drops_++`。
  - 调用方约定：`false` = 断层/脏数据/溢出，必须清书重订快照。
  - 实现（P0-1 已数组化）：双边定长排序数组 `Level bid_[64]/ask_[64]` + `bid_size_/ask_size_`，`find` 线性扫、`insert/erase` 用 `memmove`。10 档下全书常驻 1~2 个 cacheline，无 `std::map` 的跳指针/logN/分配。`depth_` 只约束查询语义不截断存储；`upsert(add=true)` 同价做加量，`erase` 不存在、`consume` 打空档均按交易所偶发做容忍（返回 true）。
- `marketdata/itch_parser.hpp`：ITCH-like 32B 定长小端练习格式 `[u8 type][u8 side][u16 pad][u32 symbol][u64 seq][i64 price][i64 qty][u64 ts]`，`type=A/C/E/S`。不是交易所真实格式，换上交所/CME 只换这一层。解析无异常无分配，失败返回 `nullopt`。已知坑：qty/ts 共用 offset 24 的 8B（`ts_exchange==qty`），演示够用，P1 改 40B 定长 `[sym4][seq8][px8][qty8][ts8]`。
- `marketdata/feed_handler.hpp/cpp`：`FeedHandler(symbol, depth, queue_pow2)`，收包→`SpscQueue`→重建最小生产链路。`on_wire()`（生产者：decode+`ts_recv`打点+进队列，满返回false记`queue_full`）+`pump()/drain()`（消费者：pop后回填`ts_book`记`queue_wait`，`apply`记`apply_lat`）。快照装配期攒`Snapshot`行、首个增量到时一次性`apply_snapshot`；`apply`失败记`gaps`，首次切回装配期记`resyncs`（实盘接告警重订）。`FeedStats{received/decoded/decode_err/queue_full/consumed/applied/gaps/resyncs/snapshots}`全量计数。
- `transport/poller.hpp`：`Poller`统一接口，Linux用`epoll`、macOS用`poll`兜底，`add_fd+wait(timeout)`语义一致。粘包/半包在`feed_pipe`按32B切包，满包才`on_wire`。
- `tools/replay.cpp`：回放 32B 文件或内置合成流（快照 4 档 + 10k 增量），统计 `apply` 时延 + `gaps`，`gaps` 即实盘清书重订点。
- `tools/bench_orderbook.cpp`：`apply()` 热路径 50 万次压测，先出标尺再谈优化。
- `tests/test_orderbook.cpp`：快照/增量 New/Change/Delete/Trade 吃量/seq 断层/交叉盘口/无快照拒绝/乱序快照有序性/同价 New 加量/64 档溢出重订 9 组断言，全用常开 `CHECK`（Release 下 `assert` 被 `NDEBUG` 禁用，教训已记）。
- `tests/test_queue.cpp`：SPSC 满背压/空/复用 + 对象池 acquire/release（同上换 `CHECK`）。
- `tools/feed_pipe.cpp`：P0-2 端到端演示，producer 线程写 pipe → `Poller::wait` → 32B 切包 → `on_wire` → `drain`，输出 `queue_wait/apply` 两套 p99 + `FeedStats` 全计数，`exit=0` 要求 `applied>0 && gaps==0`。
- `tests/test_feed.cpp`：FeedHandler 5 组——快照装配+增量apply/非法类型decode_err/容量1背压/seq断层resync后快照恢复/Poller pipe可读（epoll/poll两路）。
- `trading/order.hpp`：`Order{id,symbol,side,type,price,qty,leaves,filled,status,reject_code,ts_in}` + `Fill{order_id,symbol,side,price,qty,ts_fill}`，状态机 `New→Accepted→Resting/Partial→Filled`，`Rejected/Cancelled` 为终态。`Side` 复用行情侧 `Bid=买/Ask=卖`。
- `trading/risk.hpp`：`RiskChecker::check` 同步整数比较无 IO，`reject_code` 1=数量非法 2=超单笔 3=限价非法 4=持仓会超限 5=挂单超限。
- `trading/position.hpp`：单标的净持仓+均价+已实现盈亏，同向加权、反向先平仓、反手按成交价重建均价，`turnover/fills` 全计数。
- `trading/matcher.hpp/cpp`：`SimEngine(symbol, lim)`，`on_snapshot/on_tick` 喂行情（透传 OrderBook 断层语义）后自动碰挂单；`new_order` 先风控再即时撮合（买碰 ask≤px、卖碰 bid≥px，市价全扫，限价残留挂起、市价残留撤掉 IOC）；`cancel` 仅 Resting/Partial 可撤。`touch_resting` 用无限流动性假设（不扣 book 深度，回测用，实盘撮合由交易所做）。
- `tests/test_trading.cpp`：7 组——穿价限价即时成交/挂起后被动碰单/市价扫多档+IOC残留撤/撤单终态/5 种风控拒绝/持仓均价平仓反手/on_tick 断层透传。
- `scripts/perf_gate.sh`：性能回归门禁，跑 bench+replay+feed_pipe 并设阈值（bench p999≤500ns/mean≤120ns，feed `gaps==0 && applied>0`），超阈值非零退出，CI 直接调它。

### 3.2 编译与验证（2026-11-16 实测，P0-1 + P0-2 + P1 + 性能门禁）

```bash
cmake -S /Users/pipixia/code/code4hft -B /Users/pipixia/code/code4hft/build -DCMAKE_BUILD_TYPE=Release
cmake --build /Users/pipixia/code/code4hft/build -j
ctest --test-dir /Users/pipixia/code/code4hft/build -V
/Users/pipixia/code/code4hft/build/bench_orderbook
/Users/pipixia/code/code4hft/build/replay
/Users/pipixia/code/code4hft/build/test_feed
/Users/pipixia/code/code4hft/build/feed_pipe
./scripts/perf_gate.sh
```

结果（AppleClang 15 arm64 Mac，bench 口径：warmup 5 万 + 正式 50 万混合 op）：

```
test_orderbook PASS
test_queue PASS
test_feed PASS
test_trading PASS
iters=500000 p50=42 p99=42 p999=84 max=22167 mean=31.8 (ns) drops=0
applied=10000 gaps=0 p50=42 p99=42 p999=83 max=3375 mean=32.5 (ns)
best_bid=10000 best_ask=10010
received=20004 decoded=20004 decode_err=0 queue_full=0 consumed=20004 applied=20000 gaps=0 resyncs=0 snapshots=1
queue_wait p50=375 p99=35709 p999=73375 max=85708 mean=1937.8 (ns)
apply      p50=42 p99=83 p999=250 max=29667 mean=38.8 (ns)
best_bid=10000 best_ask=10010
GATE PASS: bench p999=83 mean=29 gaps=0 applied=20000
```

说明：`p50/p99` 在 Mac `steady_clock` 粒度（~42ns）下贴着粒度走，跨次运行会在 0/41/42 间跳，看 `mean/p999/max` 才有意义；`max` 仍是调度抖动。`-march=native` 在本机（Apple Silicon，clang 把 native 吃成通用 arm64）前后无差别属预期，Linux x86 再谈向量化差异。`feed_pipe` 的 `queue_wait p99=35709ns` 含 pipe 写→`poll`唤醒→批量`read`→`drain` 的调度等待，不是 SPSC 本体开销（SPSC push/pop 是 ns 级），看 `apply p99=83ns` 才是重建热路径真实数；`queue_full=0/gaps=0` 说明 2 万消息链路无背压无断层。

教训：Release 下 `<cassert>` 的 `assert` 被 `NDEBUG` 关掉，单测全用常开 `CHECK` 替代否则 `ctest` 全绿是假绿；`feed_pipe` 演示侧曾把 Bid/Ask 的 side 编码写反造出交叉盘口导致全链路 gaps，已修并加注释，编解码两端必须同一份常量。

已知小债务：`depth_` 仅保留作查询默认档（`bids/asks(n)` 仍由调用方传 n）；32B 练习格式 qty/ts 共用 8B（`ts_exchange==qty`），P1 改 40B 定长；Linux 上用 `taskset` 绑核 + `perf stat` 看 `cache-miss/branch-miss`、火焰图待补。

## 4. 迭代路线（按 JD 优先级）

- [x] P0-1 OrderBook 数组化：`map` → 定长数组 + 价格索引，补 `march=native` 前后 p99 对比 + 火焰图。验收：同 `bench_orderbook` 下 p99 下降且 `ctest` 全绿。对应 80-110K 必问“缓存友好 + 热路径指令级优化”。已落地：双边 `Level[64]` + `memmove`，`ctest` 双绿；Mac 上 p50/p99 贴时钟粒度，native 无差别属预期，火焰图待 Linux 补。
- [x] P0-2 收包链路：`epoll` 收包线程 → `SpscQueue` → 重建线程，`ts_exchange/ts_recv/ts_book` 三级打点，`seq` 断层触发重订计数。验收：`feed_pipe` 端到端 2 万消息 `applied=20000 gaps=0` + `apply p99=83ns`，`ctest` 三绿。对应 50-80K 国内期权/亚太链路。已落地：`FeedHandler(on_wire/pump/drain)` + `Poller(epoll/poll)` + `test_feed` 5 组。
- [x] P1 交易闭环：模拟撮合 + `Order` 生命周期 + 风控校验，对接回测。验收：下单→成交→持仓全链路单测。对应外资岗“订单管理/风控/全球分布式”。已落地：`SimEngine(new_order/cancel/on_tick)` + `RiskChecker` + `Position`，`test_trading` 7 组全绿。
- [x] P1 性能体系进 CI：`LatencyRecorder` 输出接回归阈值，`taskset` 绑核 + NUMA 对比数据，`perf stat` 看 `cache-miss/branch-miss`。验收：CI 压测抖动超阈值失败。对应 2 号岗“性能基准与回归机制”。已落地：`scripts/perf_gate.sh`（bench p999≤500ns/mean≤120ns + feed `gaps==0`），本地 `GATE PASS`；`taskset/perf` 火焰图待 Linux 补。
- [ ] P2 稳定性：7x24 常驻、容量规划、压测、故障演练（丢包/乱序/重连）、灰度回滚。验收：一份故障演练记录。

面试话术：

- 面 1/3 号：重点讲 OrderBook 重建 + 交易所协议（快照/增量语义、`seq` 重订、`Trade` 不污染 mid、ITCH 换层）。
- 面 2 号：重点讲主导的组件、无锁失效场景（SPSC 扩 MPMC 会坏、`memory_order` 用错会坏、伪共享）、eBPF 无日志定位、p999 回归。业务不懂没关系，JD 明确可在岗内补。

## 5. 变更记录

- 2026-11-16：初版骨架落地（types/clock/cacheline + SPSC/pool/latency + tick/orderbook/itch + replay/bench + 双单测），`ctest` 双绿，补基准数。
- 2026-11-16 晚 P0-1：OrderBook 去 `std::map` 换双边 `Level[64]` 排序数组（线性 find + `memmove`），新增溢出 `drops_` 重订路径；单测加到 9 组并换常开 `CHECK`（修 Release 假绿）；bench 加 warmup + 混合 op 口径；Mac 新数 `p50=41 p99=42 p999=84 mean=29.4ns`，native 无差别属预期。对标 2 号岗“缓存友好 + 热路径优化”。
- 2026-11-16 晚 P0-2：收包链路落地（`FeedHandler on_wire/pump/drain` + `Poller epoll/poll` + `feed_pipe` 端到端 + `test_feed` 5 组），三级打点 `ts_exchange/ts_recv/ts_book`，断层走 `gaps/resyncs` 重订；`ctest` 三绿；`feed_pipe` 2 万消息 `queue_full=0 gaps=0`，`apply p99=83ns`。对标 1 号岗“亚太/国内期权实盘链路”。
- 2026-11-16 晚 P1 交易闭环：`SimEngine` 落地（`order/risk/position/matcher`），下单先风控后即时撮合（限价碰价/市价扫单 IOC），挂单行情驱动被动碰单；`test_trading` 7 组（穿价成交/挂起碰单/扫多档/撤单/5 种拒绝/持仓反手/断层透传）全绿，`ctest` 四绿。对标 3 号岗“订单管理/风控”。
- 2026-11-16 晚 P1 性能门禁：`scripts/perf_gate.sh` 落地（bench p999/mean + feed gaps/applied 双阈值），本地 `GATE PASS(bench p999=83 mean=29 gaps=0 applied=20000)`。对标 2 号岗“性能基准与回归机制”。
- （后续每次迭代在此追加一行：日期 + 改了什么 + 基准变化 + 对标哪个 JD 点）
