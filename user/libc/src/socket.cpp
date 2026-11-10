// Сокеты BSD поверх docs/abi/socket.md. Нужна роль Net в стартовом сообщении.
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "internal.hpp"

using namespace libc;

namespace {

bool to_abi(const struct sockaddr* a, socklen_t len, charm::SockAddr* out) {
  if (a == nullptr || len < sizeof(sockaddr_in) || a->sa_family != AF_INET) return false;
  const auto* in = reinterpret_cast<const sockaddr_in*>(a);
  out->ipv4 = in->sin_addr.s_addr;  // уже порядок сети
  out->port = ntohs(in->sin_port);
  out->reserved = 0;
  return true;
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
  if (domain != AF_INET) return fail_errno(EAFNOSUPPORT);
  if (type != SOCK_STREAM && type != SOCK_DGRAM) return fail_errno(EINVAL);
  Handle net = start_info().net;
  if (net == 0) return fail_errno(EACCES);
  auto r = charm::socket_open(net, type == SOCK_STREAM ? 1 : 2);
  if (!r.ok()) return fail(r.status);
  int fd = fd_alloc(Kind::Socket, static_cast<Handle>(r.value), true, true);
  if (fd < 0) (void)charm::handle_close(static_cast<Handle>(r.value));
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
  if (__atomic_load_n(&f->nonblock, __ATOMIC_RELAXED)) return fail_errno(ENOTSUP);
  if (!to_abi(a, len, &s)) return fail_errno(EINVAL);
  Status st = charm::socket_connect(f->handle, &s, charm::now_ns() + 30'000'000'000ull);
  return st == Status::Ok ? 0 : fail(st);
}

ssize_t send(int fd, const void* buf, size_t n, int flags) {
  auto f = sock(fd);
  if (!f) return -1;
  return flags ? fail_errno(ENOTSUP) : write(fd, buf, n);
}
ssize_t recv(int fd, void* buf, size_t n, int flags) {
  auto f = sock(fd);
  if (!f) return -1;
  return flags ? fail_errno(ENOTSUP) : read(fd, buf, n);
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

int setsockopt(int fd, int, int, const void*, socklen_t) {
  auto f = sock(fd);
  return f ? fail_errno(ENOTSUP) : -1;
}

int getsockopt(int fd, int, int, void*, socklen_t*) {
  auto f = sock(fd);
  // CharmOS's socket ABI exposes neither SO_* state nor TCP_INFO yet; report
  // "unsupported" so callers fall back instead of reading uninitialised data.
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
