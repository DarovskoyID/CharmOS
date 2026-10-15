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

Fd* sock(int fd) {
  Fd* f = fd_get(fd);
  if (f == nullptr) {
    errno = EBADF;
    return nullptr;
  }
  if (f->kind != Kind::Socket) {
    errno = ENOTSOCK;
    return nullptr;
  }
  return f;
}

}  // namespace

extern "C" {

int socket(int domain, int type, int) {
  if (domain != AF_INET || (type != SOCK_STREAM && type != SOCK_DGRAM)) return fail_errno(EINVAL);
  Handle net = start_info().net;
  if (net == 0) return fail_errno(EACCES);
  auto r = charm::socket_open(net, type == SOCK_STREAM ? 1 : 2);
  if (!r.ok()) return fail(r.status);
  int fd = fd_alloc(Kind::Socket, static_cast<Handle>(r.value), true, true);
  if (fd < 0) (void)charm::handle_close(static_cast<Handle>(r.value));
  return fd;
}

int bind(int fd, const struct sockaddr* a, socklen_t len) {
  Fd* f = sock(fd);
  charm::SockAddr s;
  if (f == nullptr) return -1;
  if (!to_abi(a, len, &s)) return fail_errno(EINVAL);
  Status st = charm::socket_bind(f->handle, &s);
  return st == Status::Ok ? 0 : fail(st);
}

int listen(int fd, int backlog) {
  Fd* f = sock(fd);
  if (f == nullptr) return -1;
  Status st = charm::socket_listen(f->handle, static_cast<uint64_t>(backlog > 0 ? backlog : 1));
  return st == Status::Ok ? 0 : fail(st);
}

int accept(int fd, struct sockaddr* a, socklen_t* len) {
  Fd* f = sock(fd);
  if (f == nullptr) return -1;
  charm::SockAddr peer{};
  auto r = charm::socket_accept(f->handle, &peer, charm::kForever);
  if (!r.ok()) return fail(r.status);
  from_abi(peer, a, len);
  int nfd = fd_alloc(Kind::Socket, static_cast<Handle>(r.value), true, true);
  if (nfd < 0) (void)charm::handle_close(static_cast<Handle>(r.value));
  return nfd;
}

int connect(int fd, const struct sockaddr* a, socklen_t len) {
  Fd* f = sock(fd);
  charm::SockAddr s;
  if (f == nullptr) return -1;
  if (!to_abi(a, len, &s)) return fail_errno(EINVAL);
  Status st = charm::socket_connect(f->handle, &s, charm::now_ns() + 30'000'000'000ull);
  return st == Status::Ok ? 0 : fail(st);
}

ssize_t send(int fd, const void* buf, size_t n, int) { return sock(fd) != nullptr ? write(fd, buf, n) : -1; }
ssize_t recv(int fd, void* buf, size_t n, int) { return sock(fd) != nullptr ? read(fd, buf, n) : -1; }

int shutdown(int fd, int how) {
  Fd* f = sock(fd);
  if (f == nullptr) return -1;
  uint64_t h = how == SHUT_RD ? 1 : how == SHUT_WR ? 2 : 3;
  Status st = charm::socket_shutdown(f->handle, h);
  return st == Status::Ok ? 0 : fail(st);
}

int setsockopt(int, int, int, const void*, socklen_t) { return 0; }

int inet_pton(int af, const char* src, void* dst) {
  if (af != AF_INET) return -1;
  uint32_t parts[4];
  for (int i = 0; i < 4; ++i) {
    if (*src < '0' || *src > '9') return 0;
    uint32_t v = 0;
    while (*src >= '0' && *src <= '9') v = v * 10 + static_cast<uint32_t>(*src++ - '0');
    if (v > 255) return 0;
    parts[i] = v;
    if (i < 3 && *src++ != '.') return 0;
  }
  if (*src != '\0') return 0;
  auto* out = static_cast<uint8_t*>(dst);
  for (int i = 0; i < 4; ++i) out[i] = static_cast<uint8_t>(parts[i]);
  return 1;
}

const char* inet_ntop(int af, const void* src, char* dst, socklen_t size) {
  if (af != AF_INET) return nullptr;
  const auto* b = static_cast<const uint8_t*>(src);
  snprintf(dst, size, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
  return dst;
}

}  // extern "C"
