// Regression for O_NONBLOCK on TCP writes (docs/abi/libc-fd.md).
// The host-side peer connects but does not read until the guest has filled its
// send buffer. The guest verifies a short positive write followed by EAGAIN,
// then restores blocking mode and confirms the peer drains the remainder.
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

namespace {

uint64_t now_ms() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000 +
         static_cast<uint64_t>(ts.tv_nsec) / 1000000;
}

}  // namespace

extern "C" int main() {
  constexpr uint16_t kPort = 7782;
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) {
    printf("socket_nonblock: socket failed: %s\n", strerror(errno));
    return 1;
  }
  int one = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

  struct sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(kPort);
  if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0 ||
      listen(listener, 4) != 0) {
    printf("socket_nonblock: bind/listen failed: %s\n", strerror(errno));
    return 1;
  }
  int peer = accept(listener, nullptr, nullptr);
  if (peer < 0) {
    printf("socket_nonblock: accept failed: %s\n", strerror(errno));
    return 1;
  }

  if (fcntl(peer, F_SETFL, O_NONBLOCK) != 0) {
    printf("socket_nonblock: fcntl failed: %s\n", strerror(errno));
    return 1;
  }
  char block[4096];
  memset(block, 'a', sizeof block);
  size_t accepted = 0;
  bool saw_eagain = false;
  const uint64_t start = now_ms();
  for (int i = 0; i < 256; ++i) {
    ssize_t n = write(peer, block, sizeof block);
    if (n < 0) {
      if (errno == EAGAIN) {
        saw_eagain = true;
        break;
      }
      printf("socket_nonblock: nonblocking write failed: %s\n", strerror(errno));
      return 1;
    }
    if (n == 0) {
      printf("socket_nonblock: zero write\n");
      return 1;
    }
    accepted += static_cast<size_t>(n);
  }
  const uint64_t elapsed = now_ms() - start;
  if (!saw_eagain || accepted == 0 || accepted >= 256 * sizeof block || elapsed > 2000) {
    printf("socket_nonblock: bad nonblocking result accepted=%zu eagain=%d elapsed=%llu\n",
           accepted, saw_eagain ? 1 : 0, static_cast<unsigned long long>(elapsed));
    return 1;
  }

  // The host peer sleeps briefly and then drains; blocking write must finish.
  if (fcntl(peer, F_SETFL, 0) != 0) {
    printf("socket_nonblock: restore blocking failed: %s\n", strerror(errno));
    return 1;
  }
  size_t blocking_total = 0;
  while (blocking_total < 256 * sizeof block) {
    ssize_t n = write(peer, block, sizeof block);
    if (n <= 0) {
      printf("socket_nonblock: blocking write failed: %s\n", strerror(errno));
      return 1;
    }
    blocking_total += static_cast<size_t>(n);
  }

  close(peer);
  close(listener);
  printf("socket_nonblock: ok\n");
  return 0;
}
