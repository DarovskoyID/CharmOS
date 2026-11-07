#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <resolv.h>
#include <charm/libc.h>
#include <charm/network.hpp>
#include <sys/utsname.h>
#define CHECK(x) do { if (!(x)) { printf("network compatibility: failed line %d errno %d\n", __LINE__, errno); return 1; } } while (0)
extern "C" int main(int argc, char** argv) {
  bool console_audit = argc > 1 && !strcmp(argv[1], "console");
  if (console_audit) CHECK(charm_net() == 0);
  utsname identity{};
  char hostname[65]{};
  CHECK(uname(&identity) == 0 && gethostname(hostname, sizeof hostname) == 0);
  CHECK(!strcmp(hostname, identity.nodename));
  char short_name = 73;
  CHECK(gethostname(&short_name, 1) == -1 && errno == ENAMETOOLONG && short_name == 73);
  int pipes[2];
  CHECK(pipe(pipes) == 0);
  fd_set reads, writes;
  FD_ZERO(&reads); FD_ZERO(&writes);
  FD_SET(pipes[0], &reads);
  timeval immediate{};
  CHECK(select(pipes[0]+1, &reads, nullptr, nullptr, &immediate) == 0);
  CHECK(!FD_ISSET(pipes[0], &reads));
  CHECK(write(pipes[1], "x", 1) == 1);
  FD_SET(pipes[0], &reads);
  CHECK(select(pipes[0]+1, &reads, &writes, nullptr, &immediate) == 1);
  CHECK(FD_ISSET(pipes[0], &reads) && !FD_ISSET(pipes[0], &writes));
  int bytes = 0x1234;
  CHECK(ioctl(pipes[0], FIONREAD, &bytes) == -1 && errno == ENOTSUP && bytes == 0x1234);
  int flags = fcntl(pipes[0], F_GETFL);
  int yes = 1, no = 0;
  CHECK(ioctl(pipes[0], FIONBIO, &yes) == 0);
  CHECK(fcntl(pipes[0], F_GETFL) == (flags | O_NONBLOCK));
  CHECK(ioctl(pipes[0], FIONBIO, &no) == 0 && fcntl(pipes[0], F_GETFL) == flags);
  char byte;
  CHECK(read(pipes[0], &byte, 1) == 1 && byte == 'x');
  CHECK(close(pipes[0]) == 0);
  FD_ZERO(&reads); FD_SET(pipes[0], &reads);
  CHECK(select(pipes[0]+1, &reads, nullptr, nullptr, &immediate) == -1 && errno == EBADF);
  CHECK(ioctl(pipes[0], FIONREAD, &bytes) == -1 && errno == EBADF);
  CHECK(close(pipes[1]) == 0);
  timeval invalid{0, 1000000};
  CHECK(select(0, nullptr, nullptr, nullptr, &invalid) == -1 && errno == EINVAL);
  timeval tiny{0, 500};
  timespec before{}, after{};
  CHECK(clock_gettime(CLOCK_MONOTONIC, &before) == 0);
  CHECK(select(0, nullptr, nullptr, nullptr, &tiny) == 0);
  CHECK(clock_gettime(CLOCK_MONOTONIC, &after) == 0);
  CHECK((after.tv_sec-before.tv_sec)*1000000000ll + after.tv_nsec-before.tv_nsec >= 500000);
  errno = 73;
  CHECK(pthread_setname_np(pthread_self(), "test") == ENOTSUP && errno == 73);
  CHECK(if_nametoindex("lo") == 0 && (errno == ENXIO || errno == EACCES));
  CHECK(if_nametoindex("eth0") == 0 && (errno == ENXIO || errno == EACCES));
  CHECK(getifaddrs(nullptr) == -1 && errno == EFAULT);
  msghdr message{};
  CHECK(sendmsg(1, &message, 0) == -1 && errno == ENOTSOCK);
  CHECK(recvmsg(1, &message, 0) == -1 && errno == ENOTSOCK);
  addrinfo hints{}, *result = nullptr;
  hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
  CHECK(getaddrinfo("192.0.2.5", "443", &hints, &result) == 0 && result);
  char host[64], service[16];
  CHECK(getnameinfo(result->ai_addr, result->ai_addrlen, host, sizeof host,
                    service, sizeof service, NI_NUMERICHOST|NI_NUMERICSERV) == 0);
  CHECK(!strcmp(host, "192.0.2.5") && !strcmp(service, "443"));
  freeaddrinfo(result);
  hints.ai_family = AF_INET6; hints.ai_flags = AI_CANONNAME;
  CHECK(getaddrinfo("localhost", "443", &hints, &result) == 0 && result);
  CHECK(result->ai_canonname && !strcmp(result->ai_canonname, "localhost"));
  CHECK(getnameinfo(result->ai_addr, result->ai_addrlen, host, sizeof host,
                    service, sizeof service, NI_NUMERICHOST|NI_NUMERICSERV) == 0);
  CHECK(!strcmp(host, "::1") && !strcmp(service, "443"));
  freeaddrinfo(result);
  charm::NetworkInfo native{};
  if (charm_net()) {
    CHECK(charm::network_info(charm_net(), &native) == charm::Status::Ok);
    char name[IF_NAMESIZE]{};
    if (native.has_interface()) {
      CHECK(if_indextoname(1, name) == name && !strcmp(name, "net0"));
      CHECK(if_nametoindex(name) == 1);
    } else CHECK(if_indextoname(1, name) == nullptr && errno == ENXIO);
    ifaddrs* interfaces = nullptr;
    CHECK(getifaddrs(&interfaces) == 0);
    if (native.has_interface()) {
      CHECK(interfaces && !interfaces->ifa_next && !strcmp(interfaces->ifa_name, "net0"));
      if (native.ipv4) {
        CHECK(interfaces->ifa_addr && interfaces->ifa_netmask);
        CHECK(reinterpret_cast<sockaddr_in*>(interfaces->ifa_addr)->sin_addr.s_addr == native.ipv4);
      }
    } else CHECK(interfaces == nullptr);
    freeifaddrs(interfaces);
    __res_state resolver{};
    CHECK(res_ninit(&resolver) == 0);
    CHECK(resolver.nscount == (native.dns ? 1 : 0));
    if (native.dns) CHECK(resolver.nsaddr_list[0].sin_addr.s_addr == native.dns &&
                          resolver.nsaddr_list[0].sin_port == htons(53));
    CHECK(!resolver.defdname[0] && !resolver.dnsrch[0]);
    CHECK(res_nclose(&resolver) == 0 && !resolver.nscount);
  } else {
    ifaddrs* unchanged = reinterpret_cast<ifaddrs*>(uintptr_t{1});
    CHECK(getifaddrs(&unchanged) == -1 && errno == EACCES && unchanged == reinterpret_cast<ifaddrs*>(uintptr_t{1}));
    __res_state resolver{}; resolver.nscount = 73;
    CHECK(res_ninit(&resolver) == -1 && errno == EACCES && resolver.nscount == 73);
  }
  if (console_audit) printf("network compatibility: console capability isolation ok\n");
  printf("network compatibility: readiness, descriptors, honest errors and resolver ok\n");
  return 0;
}
