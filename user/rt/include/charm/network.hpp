#pragma once
#include <charm/sys.hpp>

namespace charm {
// Existing syscall ABI 1.4, docs/abi/socket.md. All IPv4 values are in
// network byte order. Zero means no DHCP/configured value, not a guessed one.
struct NetworkInfo {
  uint32_t ipv4, netmask, gateway, dns;
  uint8_t mac[6];
  uint16_t mtu;
  uint32_t flags, reserved;
  bool link_up() const { return (flags & 1u) != 0; }
  bool has_interface() const { return link_up() || ipv4 || mtu; }
};
static_assert(sizeof(NetworkInfo) == 32);
static_assert(__builtin_offsetof(NetworkInfo, flags) == 24);

// Net is borrowed. Query requires the actual capability; no global authority
// or host network is consulted. The kernel validates both object and output.
inline Status network_info(Handle net, NetworkInfo* output) {
  return syscall2(Number::NetInfo, net, reinterpret_cast<uint64_t>(output)).status;
}
// The successful result's low 32 bits contain the actual network-order IPv4
// answer. The current kernel resolver does not expose TTL or CNAME metadata.
inline SysResult resolve_ipv4(Handle net, const char* name, size_t length,
                              uint64_t deadline = kForever) {
  return syscall5(Number::DnsResolve, net, reinterpret_cast<uint64_t>(name),
                  length, deadline, 0);
}
}  // namespace charm
