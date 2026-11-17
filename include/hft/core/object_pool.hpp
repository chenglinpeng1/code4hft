#pragma once
// 定长对象池: 热路径零 malloc/free, 对标 JD: 内存池 / 缓存友好
// 用法: 收包线程从池里取 Tick/Order, 处理完归还, 全程无系统调用
#include <cassert>
#include <cstddef>
#include <vector>

namespace hft {

template <typename T>
class ObjectPool {
 public:
  explicit ObjectPool(std::size_t capacity) {
    storage_.resize(capacity);
    free_.reserve(capacity);
    for (std::size_t i = 0; i < capacity; ++i) free_.push_back(&storage_[i]);
  }

  T* acquire() {
    if (free_.empty()) return nullptr;  // 池空=背压信号, 上层计数+丢弃/扩容
    T* p = free_.back();
    free_.pop_back();
    return p;
  }

  void release(T* p) {
    assert(p >= storage_.data() && p < storage_.data() + storage_.size());
    free_.push_back(p);
  }

  std::size_t free_count() const { return free_.size(); }
  std::size_t capacity() const { return storage_.size(); }

 private:
  std::vector<T> storage_;
  std::vector<T*> free_;
};

}  // namespace hft
