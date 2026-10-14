#include "udp.hpp"

#include "../core/log.hpp"
#include "../platform/platform.hpp"
#include "../sched/sched.hpp"
#include "../sync/spinlock.hpp"
#include "ip.hpp"
#include "netif.hpp"

namespace kernel::net {

namespace {

constexpr uint8_t kProtoUdp = 17;

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
void put_be16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v);
}

struct Listener {
  uint16_t port;
  UdpHandler handler;
};
constexpr uint32_t kListeners = 16;
constinit sync::Spinlock g_lock{sync::LockRank::Block, "net.udp"};
constinit Listener g_listeners[kListeners] = {};

// Контрольная сумма UDP с псевдозаголовком (RFC 768).
uint16_t udp_checksum(uint32_t src, uint32_t dst, const uint8_t* seg, uint32_t len) {
  uint8_t pseudo[12];
  __builtin_memcpy(pseudo, &src, 4);
  __builtin_memcpy(pseudo + 4, &dst, 4);
  pseudo[8] = 0;
  pseudo[9] = kProtoUdp;
  put_be16(pseudo + 10, static_cast<uint16_t>(len));
  uint16_t first = static_cast<uint16_t>(~checksum(pseudo, sizeof pseudo));
  uint16_t sum = checksum(seg, len, first);
  return sum == 0 ? 0xffff : sum;  // 0 означает «суммы нет»
}

}  // namespace

bool udp_listen(uint16_t port, UdpHandler handler) {
  sync::SpinGuard guard(g_lock);
  for (Listener& l : g_listeners) {
    if (l.handler == nullptr) {
      l = {port, handler};
      return true;
    }
  }
  return false;
}

void udp_unlisten(uint16_t port) {
  sync::SpinGuard guard(g_lock);
  for (Listener& l : g_listeners) {
    if (l.handler != nullptr && l.port == port) l = {};
  }
}

Status udp_send(uint16_t src_port, uint32_t dst, uint16_t dst_port, const void* data, uint32_t len,
                uint64_t deadline) {
  uint8_t seg[1480];
  if (len + 8 > sizeof seg) return Status::InvalidArgs;
  put_be16(seg, src_port);
  put_be16(seg + 2, dst_port);
  put_be16(seg + 4, static_cast<uint16_t>(len + 8));
  put_be16(seg + 6, 0);
  __builtin_memcpy(seg + 8, data, len);
  put_be16(seg + 6, udp_checksum(config().ip, dst, seg, len + 8));
  return send_ip(dst, kProtoUdp, seg, len + 8, deadline);
}

void udp_input(uint32_t src, uint32_t dst, const uint8_t* seg, uint32_t len) {
  if (len < 8 || be16(seg + 4) < 8 || be16(seg + 4) > len) return;
  len = be16(seg + 4);
  if (be16(seg + 6) != 0) {  // 0 — отправитель сумму не считал (RFC 768)
    uint8_t copy[1480];
    if (len > sizeof copy) return;
    __builtin_memcpy(copy, seg, len);
    uint16_t want = be16(copy + 6);
    put_be16(copy + 6, 0);
    if (udp_checksum(src, dst, copy, len) != want) return;
  }
  uint16_t port = be16(seg + 2);
  UdpHandler h = nullptr;
  {
    sync::SpinGuard guard(g_lock);
    for (const Listener& l : g_listeners) {
      if (l.handler != nullptr && l.port == port) h = l.handler;
    }
  }
  if (h != nullptr) h(port, src, be16(seg), seg + 8, len - 8);
}

// --- DHCP (RFC 2131, 2132) -----------------------------------------------------------------

namespace {

constexpr uint16_t kClientPort = 68, kServerPort = 67;
constexpr uint8_t kDiscover = 1, kOffer = 2, kRequest = 3, kAck = 5, kNak = 6;
constexpr uint8_t kOptMask = 1, kOptRouter = 3, kOptDns = 6, kOptRequestedIp = 50;
constexpr uint8_t kOptMsgType = 53, kOptServerId = 54, kOptParams = 55, kOptEnd = 255;
constexpr uint32_t kXid = 0x43484d31;  // «CHM1»

struct Reply {
  volatile bool have;
  uint8_t type;
  uint32_t yiaddr, server, mask, router, dns;
};
constinit Reply g_reply = {};

void dhcp_handler(uint16_t, uint32_t, uint16_t, const uint8_t* d, uint32_t len) {
  if (len < 240 || d[0] != 2) return;  // op = BOOTREPLY
  uint32_t xid;
  __builtin_memcpy(&xid, d + 4, 4);
  if (xid != kXid || d[236] != 99 || d[237] != 130 || d[238] != 83 || d[239] != 99) return;
  Reply r = {};
  __builtin_memcpy(&r.yiaddr, d + 16, 4);
  for (uint32_t i = 240; i < len;) {
    uint8_t code = d[i];
    if (code == kOptEnd) break;
    if (code == 0) {  // pad
      ++i;
      continue;
    }
    if (i + 1 >= len || i + 2 + d[i + 1] > len) break;
    uint8_t olen = d[i + 1];
    const uint8_t* v = d + i + 2;
    if (code == kOptMsgType && olen == 1) r.type = v[0];
    if (code == kOptServerId && olen == 4) __builtin_memcpy(&r.server, v, 4);
    if (code == kOptMask && olen == 4) __builtin_memcpy(&r.mask, v, 4);
    if (code == kOptRouter && olen >= 4) __builtin_memcpy(&r.router, v, 4);
    if (code == kOptDns && olen >= 4) __builtin_memcpy(&r.dns, v, 4);
    i += 2u + olen;
  }
  if (r.type == 0) return;
  r.have = false;
  g_reply.type = r.type;
  g_reply.yiaddr = r.yiaddr;
  g_reply.server = r.server;
  g_reply.mask = r.mask;
  g_reply.router = r.router;
  g_reply.dns = r.dns;
  __atomic_store_n(&g_reply.have, true, __ATOMIC_RELEASE);
}

Status exchange(uint8_t type, uint32_t requested, uint32_t server, uint8_t want, uint64_t deadline) {
  uint8_t m[300] = {};
  uint8_t me[6];
  if (!mac(me)) return Status::NetUnreachable;
  m[0] = 1;  // BOOTREQUEST
  m[1] = 1;  // Ethernet
  m[2] = 6;
  __builtin_memcpy(m + 4, &kXid, 4);
  put_be16(m + 10, 0x8000);  // BROADCAST: ответ — широковещательно (у нас ещё нет адреса)
  __builtin_memcpy(m + 28, me, 6);
  m[236] = 99, m[237] = 130, m[238] = 83, m[239] = 99;
  uint32_t i = 240;
  m[i++] = kOptMsgType, m[i++] = 1, m[i++] = type;
  if (requested != 0) {
    m[i++] = kOptRequestedIp, m[i++] = 4;
    __builtin_memcpy(m + i, &requested, 4);
    i += 4;
  }
  if (server != 0) {
    m[i++] = kOptServerId, m[i++] = 4;
    __builtin_memcpy(m + i, &server, 4);
    i += 4;
  }
  m[i++] = kOptParams, m[i++] = 3, m[i++] = kOptMask, m[i++] = kOptRouter, m[i++] = kOptDns;
  m[i++] = kOptEnd;
  for (uint64_t next = 0;;) {
    uint64_t now = platform::now_ns();
    if (now >= deadline) return Status::TimedOut;
    if (now >= next) {
      __atomic_store_n(&g_reply.have, false, __ATOMIC_RELEASE);
      (void)udp_send(kClientPort, 0xffffffffu, kServerPort, m, i, deadline);
      next = now + 1'000'000'000;
    }
    if (__atomic_load_n(&g_reply.have, __ATOMIC_ACQUIRE)) {
      if (g_reply.type == want) return Status::Ok;
      if (g_reply.type == kNak) return Status::ConnectionRefused;
      __atomic_store_n(&g_reply.have, false, __ATOMIC_RELEASE);
    }
    sched::sleep_ns(5'000'000);
  }
}

}  // namespace

Status dhcp_configure(uint64_t deadline) {
  if (!udp_listen(kClientPort, &dhcp_handler)) return Status::NoMemory;
  Status s = exchange(kDiscover, 0, 0, kOffer, deadline);
  if (s == Status::Ok) {
    uint32_t offered = g_reply.yiaddr, server = g_reply.server;
    s = exchange(kRequest, offered, server, kAck, deadline);
  }
  udp_unlisten(kClientPort);
  if (s != Status::Ok) return s;
  configure({g_reply.yiaddr, g_reply.mask, g_reply.router, g_reply.dns});
  const auto* a = reinterpret_cast<const uint8_t*>(&g_reply.yiaddr);
  const auto* gw = reinterpret_cast<const uint8_t*>(&g_reply.router);
  kprint("net: dhcp address {}.{}.{}.{} gateway {}.{}.{}.{}\n", a[0], a[1], a[2], a[3], gw[0], gw[1],
         gw[2], gw[3]);
  return Status::Ok;
}

}  // namespace kernel::net
