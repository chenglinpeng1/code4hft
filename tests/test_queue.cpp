// 无 gtest 依赖的最小断言集: Release 下 assert 被 NDEBUG 禁用, 用常开 CHECK
#include <cstdio>

#include "hft/core/object_pool.hpp"
#include "hft/core/spsc_queue.hpp"

#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) {                                                        \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);         \
      return 1;                                                        \
    }                                                                  \
  } while (0)

using namespace hft;

int main() {
  SpscQueue<int> q(8);
  CHECK(q.empty());
  for (int i = 0; i < 8; ++i) CHECK(q.push(i));
  CHECK(!q.push(99));  // 满背压
  for (int i = 0; i < 8; ++i) {
    int v = -1;
    CHECK(q.pop(v) && v == i);
  }
  int v = -1;
  CHECK(!q.pop(v));  // 空
  // 复用一轮
  CHECK(q.push(42) && q.pop(v) && v == 42);

  ObjectPool<int> pool(4);
  CHECK(pool.free_count() == 4);
  int* p = pool.acquire();
  CHECK(p && pool.free_count() == 3);
  pool.release(p);
  CHECK(pool.free_count() == 4);

  std::printf("test_queue PASS\n");
  return 0;
}
