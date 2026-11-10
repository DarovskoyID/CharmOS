// Сокеты BSD поверх docs/abi/socket.md. Нужна роль Net в стартовом сообщении.
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "internal.hpp"

using namespace libc;

namespace {

bool to_abi(const struct sockaddr* a, socklen_t len, charm::SockAddr* out) {
  if (a == nullptr) return false;
  if (a->sa_family == AF_INET && len >= sizeof(sockaddr_in)) {
    const auto* in = reinterpret_cast<const sockaddr_in*>(a);
    out->ipv4 = in->sin_addr.s_addr;
    out->port = ntohs(in->sin_port);
    out->reserved = 0;
    return true;
  }
  // CharmOS sockets are IPv4-only. Chromium still probes/binds AF_INET6
  // endpoints for DNS and dual-stack connect; map the wildcard, loopback and
  // v4-mapped forms instead of failing the whole network request.
  if (a->sa_family == AF_INET6 && len >= sizeof(sockaddr_in6)) {
    const auto* in6 = reinterpret_cast<const sockaddr_in6*>(a);
    const uint8_t* b = in6->sin6_addr.s6_addr;
    uint32_t ipv4 = 0;
    bool v4mapped = true;
    for (int i = 0; i < 10; ++i) v4mapped = v4mapped && b[i] == 0;
    if (v4mapped && b[10] == 0xff && b[11] == 0xff) {
      memcpy(&ipv4, b + 12, 4);
    } else {
      bool any = true;
      for (int i = 0; i < 16; ++i) any = any && b[i] == 0;
      bool loopback = true;
      for (int i = 0; i < 15; ++i) loopback = loopback && b[i] == 0;
      loopback = loopback && b[15] == 1;
      if (any) {
        ipv4 = 0;
      } else if (loopback) {
        ipv4 = htonl(0x7f000001u);
      } else {
        return false;
      }
    }
    out->ipv4 = ipv4;
    out->port = ntohs(in6->sin6_port);
    out->reserved = 0;
    return true;
  }
  return false;
}

void from_abi(const charm::SockAddr& s, struct sockaddr* a, socklen_t* len) {
  if (a == nullptr || len == nullptr || *len < sizeof(sockaddr_in)) return;
  auto* in = reinterpret_cast<sockaddr_in*>(a);
  memset(in, 0, sizeof *in);
  in->sin_family = AF_INET;
  in->sin_port = htons(s.port);
  in->sin_addr.s_addr = s.ipv4;
  *len = sizeof(sockaddr_in);
}

FdLease sock(int fd) {
  FdLease f(fd);
  if (!f) {
    errno = EBADF;
    return {};
  }
  if (f->kind != Kind::Socket) {
    errno = ENOTSOCK;
    return {};
  }
  return f;
}

}  // namespace

extern "C" {

int socket(int domain, int type, int) {
  // Dual-stack probes use AF_INET6; the native transport is IPv4. Requests
  // with a real IPv6 peer are rejected by to_abi(), while wildcard/v4-mapped
  // addresses work as IPv4 sockets.
  if (domain == AF_INET6) domain = AF_INET;
  if (domain != AF_INET) return fail_errno(EAFNOSUPPORT);
  int flags = type & (SOCK_NONBLOCK | SOCK_CLOEXEC);
  type &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (type != SOCK_STREAM && type != SOCK_DGRAM) return fail_errno(EINVAL);
  Handle net = start_info().net;
  if (net == 0) return fail_errno(EACCES);
  auto r = charm::socket_open(net, type == SOCK_STREAM ? 1 : 2);
  if (!r.ok()) return fail(r.status);
  int fd = fd_alloc(Kind::Socket, static_cast<Handle>(r.value), true, true);
  if (fd < 0) {
    (void)charm::handle_close(static_cast<Handle>(r.value));
    return -1;
  }
  // The native connect path is blocking; Chromium switches the descriptor to
  // nonblocking after connect. Applying SOCK_NONBLOCK here would make connect
  // fail with ENOTSUP, so only the close-on-exec bit is applied eagerly.
  if (flags & SOCK_CLOEXEC) (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
  return fd;
}

int bind(int fd, const struct sockaddr* a, socklen_t len) {
  auto f = sock(fd);
  charm::SockAddr s;
  if (!f) return -1;
  if (!to_abi(a, len, &s)) return fail_errno(EINVAL);
  Status st = charm::socket_bind(f->handle, &s);
  return st == Status::Ok ? 0 : fail(st);
}

int listen(int fd, int backlog) {
  auto f = sock(fd);
  if (!f) return -1;
  Status st = charm::socket_listen(f->handle, static_cast<uint64_t>(backlog > 0 ? backlog : 1));
  return st == Status::Ok ? 0 : fail(st);
}

int accept(int fd, struct sockaddr* a, socklen_t* len) {
  auto f = sock(fd);
  if (!f) return -1;
  charm::SockAddr peer{};
  bool nonblock = __atomic_load_n(&f->nonblock, __ATOMIC_RELAXED);
  auto r = charm::socket_accept(f->handle, &peer, nonblock ? 0 : charm::kForever);
  if (!r.ok()) return nonblock && r.status == Status::TimedOut ? fail_errno(EAGAIN) : fail(r.status);
  from_abi(peer, a, len);
  int nfd = fd_alloc(Kind::Socket, static_cast<Handle>(r.value), true, true);
  if (nfd < 0) (void)charm::handle_close(static_cast<Handle>(r.value));
  return nfd;
}

int connect(int fd, const struct sockaddr* a, socklen_t len) {
  auto f = sock(fd);
  charm::SockAddr s;
  if (!f) return -1;
  if (!to_abi(a, len, &s)) return fail_errno(EINVAL);
  // The native TCP stack has no asynchronous handshake: connect runs to
  // completion (up to its own 30 s deadline) even on a nonblocking
  // descriptor. Chromium issues connect() after switching the socket to
  // nonblocking mode and accepts a synchronous success, which is what this
  // reports. The descriptor keeps O_NONBLOCK, so the following sends and
  // receives still return EAGAIN and are driven by poll.
  Status st = charm::socket_connect(f->handle, &s, charm::now_ns() + 30'000'000'000ull);
  return st == Status::Ok ? 0 : fail(st);
}

ssize_t send(int fd, const void* buf, size_t n, int flags) {
  auto f = sock(fd);
  if (!f) return -1;
  // MSG_NOSIGNAL has no effect: CharmOS never raises SIGPIPE for sockets.
  // MSG_DONTWAIT is implied by the descriptor's O_NONBLOCK state.
  if (flags & ~(MSG_NOSIGNAL | MSG_DONTWAIT)) return fail_errno(ENOTSUP);
  return write(fd, buf, n);
}
ssize_t recv(int fd, void* buf, size_t n, int flags) {
  auto f = sock(fd);
  if (!f) return -1;
  if (flags & ~(MSG_DONTWAIT | MSG_PEEK)) return fail_errno(ENOTSUP);
  if (n == 0) return 0;
  auto* out = static_cast<unsigned char*>(buf);
  if (f->peeked) {
    out[0] = f->peek_byte;
    if ((flags & MSG_PEEK) == 0) f->peeked = false;
    return 1;
  }
  if ((flags & MSG_PEEK) == 0) return read(fd, buf, n);
  // The kernel ABI has no peek flag: take one byte now and hand it out again
  // from the description's pushback slot. This is what Chromium's
  // SocketPosix::IsConnected() probes with.
  unsigned char byte = 0;
  auto r = charm::socket_recv(f->handle, &byte, 1,
                              __atomic_load_n(&f->nonblock, __ATOMIC_RELAXED)
                                  ? 0
                                  : charm::kForever);
  if (!r.ok()) {
    if (r.status == Status::PeerClosed) return 0;
    if (r.status == Status::WouldBlock || r.status == Status::TimedOut)
      return fail_errno(EAGAIN);
    return fail(r.status);
  }
  if (r.value == 0) return 0;
  f->peek_byte = byte;
  f->peeked = true;
  out[0] = byte;
  return 1;
}

int socketpair(int domain, int, int, int*) {
  // Unix-сокеты не реализованы: дескрипторы передаются по каналам ядра
  // (docs/abi/ipc.md), а не через AF_UNIX/SCM_RIGHTS.
  (void)domain;
  return fail_errno(EAFNOSUPPORT);
}

ssize_t sendmsg(int fd, const struct msghdr* msg, int flags) {
  auto f = sock(fd);
  if (!f) return -1;
  if (msg == nullptr) return fail_errno(EFAULT);
  (void)flags;
  // Separate writev writes cannot preserve native datagram boundaries.
  return fail_errno(ENOTSUP);
}

ssize_t recvmsg(int fd, struct msghdr* msg, int) {
  auto f = sock(fd);
  if (!f) return -1;
  if (msg == nullptr) return fail_errno(EFAULT);
  // Needs a native atomic message receive, including flags and truncation.
  return fail_errno(ENOTSUP);
}

int getsockname(int, struct sockaddr*, socklen_t*) {
  return fail_errno(ENOTSUP);  // локальный адрес ещё не выдаёт ядро
}

int getpeername(int, struct sockaddr*, socklen_t*) {
  return fail_errno(ENOTSUP);
}

int shutdown(int fd, int how) {
  auto f = sock(fd);
  if (!f) return -1;
  if (how < SHUT_RD || how > SHUT_RDWR) return fail_errno(EINVAL);
  uint64_t h = how == SHUT_RD ? 1 : how == SHUT_WR ? 2 : 3;
  Status st = charm::socket_shutdown(f->handle, h);
  return st == Status::Ok ? 0 : fail(st);
}

int setsockopt(int fd, int level, int name, const void*, socklen_t) {
  auto f = sock(fd);
  if (!f) return -1;
  // These options only tune the peer's transport heuristics. The native
  // transport always uses its fixed retransmit/keepalive policy, so accept
  // them instead of failing a connection setup that Chromium otherwise treats
  // as fatal (notably TCP_NODELAY and SO_KEEPALIVE).
  if (level == SOL_SOCKET &&
      (name == SO_REUSEADDR || name == SO_KEEPALIVE || name == SO_BROADCAST ||
       name == SO_SNDBUF || name == SO_RCVBUF || name == SO_LINGER)) {
    return 0;
  }
  if (level == IPPROTO_TCP && name == TCP_NODELAY) return 0;
  return fail_errno(ENOTSUP);
}

int getsockopt(int fd, int, int, void*, socklen_t*) {
  auto f = sock(fd);
  // CharmOS's socket ABI exposes neither SO_* state nor TCP_INFO yet; report
  // "unsupported" so callers fall back instead of reading uninitialised data.
  // In particular no SO_ERROR value is invented: this adapter never returns
  // EINPROGRESS from connect (see connect below), so callers never poll it.
  return f ? fail_errno(ENOTSUP) : -1;
}

ssize_t sendto(int fd, const void* data, size_t size, int flags,
               const struct sockaddr* address, socklen_t) {
  auto f = sock(fd);
  if (!f) return -1;
  // Connected sockets route through the native send path. The kernel has no
  // address argument for unconnected datagrams, so those are rejected
  // explicitly rather than silently delivered to the wrong peer.
  if (address == nullptr) return send(fd, data, size, flags);
  return fail_errno(ENOTSUP);
}

}  // extern "C"
