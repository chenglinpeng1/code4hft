#pragma once
// Poller: Linux 用 epoll, macOS 用 poll 兜底, 接口统一
// 对标 JD: epoll / 高吞吐低抖动的收发
// 用法: 加一个 fd(管道/网口/socket), wait() 到事件后读 32B 定长消息
#include <cstdint>

#if defined(__linux__)
#include <sys/epoll.h>
#include <unistd.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

namespace hft {

class Poller {
 public:
  Poller() {
#if defined(__linux__)
    efd_ = epoll_create1(EPOLL_CLOEXEC);
#endif
  }
  ~Poller() {
#if defined(__linux__)
    if (efd_ >= 0) ::close(efd_);
#endif
  }
  Poller(const Poller&) = delete;
  Poller& operator=(const Poller&) = delete;

  bool valid() const {
#if defined(__linux__)
    return efd_ >= 0;
#else
    return true;  // poll 无句柄
#endif
  }

  bool add_fd(int fd) {
#if defined(__linux__)
    if (efd_ < 0) return false;
    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    return epoll_ctl(efd_, EPOLL_CTL_ADD, fd, &ev) == 0;
#else
    fd_ = fd;
    return fd >= 0;
#endif
  }

  // 等待可读: 返回 >0 可读, 0 超时, <0 出错
  int wait(int timeout_ms) {
#if defined(__linux__)
    struct epoll_event ev{};
    return epoll_wait(efd_, &ev, 1, timeout_ms);
#else
    struct pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;
    return ::poll(&pfd, 1, timeout_ms);
#endif
  }

 private:
#if defined(__linux__)
  int efd_ = -1;
#else
  int fd_ = -1;
#endif
};

}  // namespace hft
