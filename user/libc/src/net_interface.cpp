#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <resolv.h>
#include <stdlib.h>
#include <string.h>
#include "net_info.hpp"

namespace {
constexpr char kName[] = "net0";
struct Interface {
  ifaddrs entry;
  sockaddr_in address, mask, broadcast;
  char name[sizeof(kName)];
};
}
extern "C" {
unsigned if_nametoindex(const char* name) {
  if (!name) { errno = EFAULT; return 0; }
  libc::NetInfo info{};
  if (libc::query_net_info(&info) < 0) return 0;
  if (!libc::has_interface(info) || strcmp(name, kName)) { errno = ENXIO; return 0; }
  return 1;
}
char* if_indextoname(unsigned index, char* name) {
  if (!name) { errno = EFAULT; return nullptr; }
  libc::NetInfo info{};
  if (libc::query_net_info(&info) < 0) return nullptr;
  if (index != 1 || !libc::has_interface(info)) { errno = ENXIO; return nullptr; }
  strcpy(name, kName);
  return name;
}
int getifaddrs(ifaddrs** output) {
  if (!output) return libc::fail_errno(EFAULT);
  libc::NetInfo info{};
  if (libc::query_net_info(&info) < 0) return -1;
  if (!libc::has_interface(info)) { *output = nullptr; return 0; }
  auto* node = static_cast<Interface*>(calloc(1, sizeof(Interface)));
  if (!node) return libc::fail_errno(ENOMEM);
  strcpy(node->name, kName);
  node->entry.ifa_name = node->name;
  node->entry.ifa_flags = IFF_BROADCAST;
  if (info.flags & 1) node->entry.ifa_flags |= IFF_UP | IFF_RUNNING;
  if (info.ipv4) {
    node->address.sin_family = node->mask.sin_family = node->broadcast.sin_family = AF_INET;
    node->address.sin_addr.s_addr = info.ipv4;
    node->mask.sin_addr.s_addr = info.netmask;
    node->broadcast.sin_addr.s_addr = info.ipv4 | ~info.netmask;
    node->entry.ifa_addr = reinterpret_cast<sockaddr*>(&node->address);
    node->entry.ifa_netmask = reinterpret_cast<sockaddr*>(&node->mask);
    node->entry.ifa_broadaddr = reinterpret_cast<sockaddr*>(&node->broadcast);
  }
  *output = &node->entry;
  return 0;
}
void freeifaddrs(ifaddrs* entry) {
  while (entry) { auto* next = entry->ifa_next; free(entry); entry = next; }
}
__res_state _res{};
int res_ninit(__res_state* state) {
  if (!state) return libc::fail_errno(EFAULT);
  libc::NetInfo info{};
  if (libc::query_net_info(&info) < 0) return -1;
  __res_state result{};
  result.options = RES_INIT | RES_RECURSE;
  result.retrans = 5;
  result.retry = 2;
  result.ndots = 1;
  if (info.dns) {
    result.nscount = 1;
    result.nsaddr_list[0].sin_family = AF_INET;
    result.nsaddr_list[0].sin_port = htons(53);
    result.nsaddr_list[0].sin_addr.s_addr = info.dns;
  }
  *state = result;
  return 0;
}
int res_nclose(__res_state* state) {
  if (!state) return libc::fail_errno(EFAULT);
  memset(state, 0, sizeof(*state));
  return 0;
}
void res_ndestroy(__res_state* state) { if (state) (void)res_nclose(state); }
int res_init() { return res_ninit(&_res); }
}
