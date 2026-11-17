# code4hft

C++20 高频交易系统骨架：L2 行情重建 → 订单簿 → 模拟撮合 → 持仓/风控。

对标上海高频岗位（50-110K 区间）的技能树：现代 C++、内存模型与无锁队列、
Linux IO、Level2 盘口重建、交易协议、性能基准与回归机制。

## 现状

| 模块 | 说明 |
|---|---|
| `include/hft/common/` | 定点价格 `int64`、纳秒时钟、cacheline 隔离 |
| `include/hft/core/` | SPSC 无锁队列、定长对象池、延迟直方图 |
| `include/hft/marketdata/` | L2 Tick、数组化 OrderBook、ITCH-like 解析、收包链路 |
| `include/hft/transport/` | epoll / poll 统一 Poller |
| `include/hft/trading/` | Order 生命周期、风控、持仓、模拟撮合 |

## 构建与测试

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build -V
```

## 工具

```bash
./build/bench_orderbook   # apply() 热路径压测, 输出 p50/p99/p999
./build/replay            # 回放 32B ITCH-like 流, 统计 gaps 与时延
./build/feed_pipe         # 端到端: pipe -> Poller -> SPSC -> OrderBook
./scripts/perf_gate.sh    # 性能回归门禁, 超阈值非零退出
```

## 关键设计

- **价格定点**：`PriceTick = int64`（px * 1e4），撮合键不用 `double`
- **OrderBook 数组化**：双边 `Level[64]` 排序数组，线性查找 + `memmove`，
  10 档下全书常驻 1~2 个 cacheline
- **断层可发现**：增量须严格 `seq == last_seq+1`，否则返回 false 让上层
  清书重订快照
- **三级打点**：`ts_exchange` → `ts_recv` → `ts_book`，`queue_wait` 可量化
- **风控前置**：下单先校验数量/单笔上限/限价/持仓/挂单数，再进撮合

## 路线

- [x] OrderBook 数组化 + 压测基准
- [x] 收包链路（epoll + SPSC + 三级打点 + 断层重订）
- [x] 模拟撮合 + 风控 + 持仓
- [x] 性能回归门禁
- [ ] Linux `taskset` 绑核 / `perf stat` cache-miss 分析 / 火焰图
- [ ] 40B 定长 wire 格式（当前 32B 练习格式 qty/ts 共用 8B）
- [ ] 7x24 稳定性：容量规划、故障演练、灰度回滚

详见 `tests/PLAN.md`。
