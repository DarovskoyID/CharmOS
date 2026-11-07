#ifndef _IFADDRS_H
#define _IFADDRS_H
#include <sys/socket.h>
struct ifaddrs {
  struct ifaddrs* ifa_next;
  char* ifa_name;
  unsigned int ifa_flags;
  struct sockaddr* ifa_addr;
  struct sockaddr* ifa_netmask;
  struct sockaddr* ifa_broadaddr;
  void* ifa_data;
};
#define IFF_UP 0x1
#define IFF_BROADCAST 0x2
#define IFF_LOOPBACK 0x8
#define IFF_RUNNING 0x40
#ifdef __cplusplus
extern "C" {
#endif
int getifaddrs(struct ifaddrs** ifap);
void freeifaddrs(struct ifaddrs* ifa);
#ifdef __cplusplus
}
#endif
#endif
