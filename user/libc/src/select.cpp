#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <sys/select.h>
#include "internal.hpp"

extern "C" int select(int nfds, fd_set* read, fd_set* write,
                      fd_set* except, struct timeval* timeout) {
  if (nfds < 0 || nfds > FD_SETSIZE ||
      (timeout && (timeout->tv_sec < 0 || timeout->tv_usec < 0 ||
                   timeout->tv_usec >= 1000000))) return libc::fail_errno(EINVAL);
  pollfd polls[64]{}; // Native poll supports 64 simultaneous watches.
  int count = 0;
  for (int fd = 0; fd < nfds; ++fd) {
    short events = 0;
    if (read && FD_ISSET(fd, read)) events |= POLLIN;
    if (write && FD_ISSET(fd, write)) events |= POLLOUT;
    if (except && FD_ISSET(fd, except)) events |= POLLPRI;
    if (!events) continue;
    libc::FdLease value(fd);
    if (!value) return libc::fail_errno(EBADF);
    if (count == 64) return libc::fail_errno(EINVAL);
    polls[count++] = {fd, events, 0};
  }
  uint64_t remaining = 0;
  if (timeout) {
    uint64_t seconds = static_cast<uint64_t>(timeout->tv_sec);
    uint64_t fraction = (static_cast<uint64_t>(timeout->tv_usec) + 999) / 1000;
    remaining = seconds > (UINT64_MAX - fraction) / 1000
        ? UINT64_MAX : seconds * 1000 + fraction;
  }
  for (;;) {
    int wait = !timeout ? -1 : remaining > INT_MAX ? INT_MAX : static_cast<int>(remaining);
    int result = poll(polls, static_cast<nfds_t>(count), wait);
    if (result < 0) return -1;
    for (int i = 0; i < count; ++i)
      if (polls[i].revents & POLLNVAL) return libc::fail_errno(EBADF);
    if (result || !timeout || remaining <= static_cast<uint64_t>(wait)) break;
    remaining -= static_cast<uint64_t>(wait);
  }
  if (read) FD_ZERO(read);
  if (write) FD_ZERO(write);
  if (except) FD_ZERO(except);
  int total = 0;
  for (int i = 0; i < count; ++i) {
    const auto& p = polls[i];
    if (read && (p.events & POLLIN) && (p.revents & (POLLIN|POLLHUP|POLLERR))) {
      FD_SET(p.fd, read); ++total;
    }
    if (write && (p.events & POLLOUT) && (p.revents & (POLLOUT|POLLERR))) {
      FD_SET(p.fd, write); ++total;
    }
    if (except && (p.events & POLLPRI) && (p.revents & POLLPRI)) {
      FD_SET(p.fd, except); ++total;
    }
  }
  return total;
}
