#!/bin/bash
# perf_gate: 性能回归门禁, 对应 PLAN P1"性能体系进 CI"
# 跑 bench + replay + feed_pipe, 解析输出, 超阈值即失败
# Mac steady_clock 粒度 ~42ns, 阈值按 mean/p999 设, p50/p99 仅展示
set -e
BUILD_DIR="${1:-/Users/pipixia/code/code4hft/build}"
BENCH_P999_MAX=500
BENCH_MEAN_MAX=120
APPLY_P999_MAX=1000
APPLY_MEAN_MAX=150

echo "== bench_orderbook =="
BENCH_OUT=$("$BUILD_DIR/bench_orderbook")
echo "$BENCH_OUT"
BENCH_P999=$(echo "$BENCH_OUT" | sed -n 's/.*p999=\([0-9]*\).*/\1/p')
BENCH_MEAN=$(echo "$BENCH_OUT" | sed -n 's/.*mean=\([0-9.]*\).*/\1/p' | cut -d. -f1)
if [ "$BENCH_P999" -gt "$BENCH_P999_MAX" ]; then echo "GATE FAIL: bench p999=$BENCH_P999 > $BENCH_P999_MAX"; exit 1; fi
if [ "$BENCH_MEAN" -gt "$BENCH_MEAN_MAX" ]; then echo "GATE FAIL: bench mean=$BENCH_MEAN > $BENCH_MEAN_MAX"; exit 1; fi

echo "== replay =="
"$BUILD_DIR/replay" >/dev/null
echo "replay ok"

echo "== feed_pipe =="
PIPE_OUT=$("$BUILD_DIR/feed_pipe")
echo "$PIPE_OUT"
GAPS=$(echo "$PIPE_OUT" | sed -n 's/.*gaps=\([0-9]*\).*/\1/p')
APPLIED=$(echo "$PIPE_OUT" | sed -n 's/.*applied=\([0-9]*\).*/\1/p' | head -n1)
if [ "$GAPS" != "0" ]; then echo "GATE FAIL: feed gaps=$GAPS"; exit 1; fi
if [ "$APPLIED" = "0" ]; then echo "GATE FAIL: feed applied=0"; exit 1; fi

echo "GATE PASS: bench p999=$BENCH_P999 mean=$BENCH_MEAN gaps=$GAPS applied=$APPLIED"
