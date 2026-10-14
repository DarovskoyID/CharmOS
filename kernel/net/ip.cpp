#include "ip.hpp"

#include "../core/log.hpp"
#include "../platform/platform.hpp"
#include "../sched/sched.hpp"
#include "../sync/spinlock.hpp"
#include "netif.hpp"

namespace kernel::net {

namespace {

// Ethernet II (RFC 894): 6 байт назначение, 6 источник, 2 тип.
constexpr uint16_t kEtherIpv4 = 0x0800;
constexpr uint16_t kEtherArp = 0x0806;
constexpr uint32_t kEthHeader = 14;
constexpr uint8_t kBroadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
// ARP (RFC 826) для Ethernet и IPv4: hrd = 1, pro = 0x0800, hln = 6, pln = 4.
constexpr uint16_t kArpRequest = 1;
constexpr uint16_t kArpReply = 2;
// IPv4 (RFC 791), ICMP (RFC 792).
constexpr uint8_t kProtoIcmp = 1;
constexpr uint8_t kIcmpEchoReply = 0;
constexpr uint8_t kIcmpEcho = 8;

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }
void put_be16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v >> 8);
  p[1] = static_cast<uint8_t>(v);
}
uint32_t get32(const uint8_t* p) {
  uint32_t v;
  __builtin_memcpy(&v, p, 4);
  return v;
}

struct ArpEntry {
  uint32_t ip;
  uint8_t mac[6];
  bool valid;
};

constexpr uint32_t kArpEntries = 16;
constinit sync::Spinlock g_lock{sync::LockRank::Block, "net.ip"};
constinit Config g_cfg = {};
constinit ArpEntry g_arp[kArpEntries] = {};
constinit uint32_t g_arp_next = 0;
constinit uint16_t g_ip_id = 1;
// Ожидающий ping: один за раз (для тестов и диагностики).
constinit volatile uint16_t g_ping_seq = 0;
constinit volatile uint32_t g_ping_from = 0;
constinit volatile bool g_ping_done = false;

bool arp_lookup(uint32_t ip, uint8_t* mac) {
  sync::SpinGuard guard(g_lock);
  for (const ArpEntry& e : g_arp) {
    if (e.valid && e.ip == ip) {
      __builtin_memcpy(mac, e.mac, 6);
      return true;
    }
  }
  return false;
}

void arp_learn(uint32_t ip, const uint8_t* mac) {
  sync::SpinGuard guard(g_lock);
  for (ArpEntry& e : g_arp) {
    if (e.valid && e.ip == ip) {
      __builtin_memcpy(e.mac, mac, 6);
      return;
    }
  }
  ArpEntry& e = g_arp[g_arp_next++ % kArpEntries];
  e = {ip, {}, true};
  __builtin_memcpy(e.mac, mac, 6);
}

void send_arp(uint16_t op, const uint8_t* dst_mac, uint32_t target_ip, const uint8_t* target_mac) {
  uint8_t f[kEthHeader + 28] = {};
  uint8_t me[6];
  if (!mac(me)) return;
  __builtin_memcpy(f, dst_mac, 6);
  __builtin_memcpy(f + 6, me, 6);
  put_be16(f + 12, kEtherArp);
  uint8_t* a = f + kEthHeader;
  put_be16(a, 1);
  put_be16(a + 2, kEtherIpv4);
  a[4] = 6;
  a[5] = 4;
  put_be16(a + 6, op);
  __builtin_memcpy(a + 8, me, 6);
  uint32_t my_ip = config().ip;
  __builtin_memcpy(a + 14, &my_ip, 4);
  if (target_mac != nullptr) __builtin_memcpy(a + 18, target_mac, 6);
  __builtin_memcpy(a + 24, &target_ip, 4);
  (void)send_frame(f, sizeof f);
}

// MAC следующего узла для dst: сам dst в своей сети, иначе шлюз.
Status resolve(uint32_t dst, uint8_t* out, uint64_t deadline) {
  Config c = config();
  uint32_t hop = (dst & c.netmask) == (c.ip & c.netmask) ? dst : c.gateway;
  if (hop == 0) return Status::NetUnreachable;
  for (uint64_t next = 0;;) {
    if (arp_lookup(hop, out)) return Status::Ok;
    uint64_t now = platform::now_ns();
    if (now >= deadline) return Status::TimedOut;
    if (now >= next) {
      send_arp(kArpRequest, kBroadcast, hop, nullptr);
      next = now + 200'000'000;
    }
    sched::sleep_ns(1'000'000);
  }
}

}  // namespace

uint16_t checksum(const void* data, uint32_t len, uint32_t initial) {
  const auto* p = static_cast<const uint8_t*>(data);
  uint32_t sum = initial;
  for (uint32_t i = 0; i + 1 < len; i += 2) sum += be16(p + i);
  if (len % 2 != 0) sum += uint32_t{p[len - 1]} << 8;
  while ((sum >> 16) != 0) sum = (sum & 0xffff) + (sum >> 16);
  return static_cast<uint16_t>(~sum);
}

void configure(const Config& c) {
  sync::SpinGuard guard(g_lock);
  g_cfg = c;
}

Config config() {
  sync::SpinGuard guard(g_lock);
  return g_cfg;
}

Status send_ip(uint32_t dst, uint8_t proto, const void* payload, uint32_t len, uint64_t deadline) {
  if (len + 20 + kEthHeader > 1514) return Status::InvalidArgs;  // без фрагментации (v1.4)
  uint8_t next_mac[6];
  Status s = resolve(dst, next_mac, deadline);
  if (s != Status::Ok) return s == Status::TimedOut ? Status::NetUnreachable : s;
  uint8_t f[1514];
  uint8_t me[6];
  if (!mac(me)) return Status::NetUnreachable;
  __builtin_memcpy(f, next_mac, 6);
  __builtin_memcpy(f + 6, me, 6);
  put_be16(f + 12, kEtherIpv4);
  uint8_t* ip = f + kEthHeader;
  __builtin_memset(ip, 0, 20);
  ip[0] = 0x45;  // версия 4, IHL 5
  put_be16(ip + 2, static_cast<uint16_t>(20 + len));
  uint16_t id;
  {
    sync::SpinGuard guard(g_lock);
    id = g_ip_id++;
  }
  put_be16(ip + 4, id);
  put_be16(ip + 6, 0x4000);  // DF
  ip[8] = 64;                // TTL
  ip[9] = proto;
  uint32_t src = config().ip;
  __builtin_memcpy(ip + 12, &src, 4);
  __builtin_memcpy(ip + 16, &dst, 4);
  put_be16(ip + 10, checksum(ip, 20));
  __builtin_memcpy(ip + 20, payload, len);
  return send_frame(f, kEthHeader + 20 + len);
}

Status ping(uint32_t dst, uint16_t seq, uint64_t deadline, uint64_t* rtt_ns) {
  uint8_t icmp[8 + 32];
  for (uint32_t i = 8; i < sizeof icmp; ++i) icmp[i] = static_cast<uint8_t>(i);
  icmp[0] = kIcmpEcho;
  icmp[1] = 0;
  put_be16(icmp + 2, 0);
  put_be16(icmp + 4, 0x4348);  // идентификатор «CH»
  put_be16(icmp + 6, seq);
  put_be16(icmp + 2, checksum(icmp, sizeof icmp));
  g_ping_done = false;
  g_ping_seq = seq;
  g_ping_from = dst;
  uint64_t start = platform::now_ns();
  Status s = send_ip(dst, kProtoIcmp, icmp, sizeof icmp, deadline);
  if (s != Status::Ok) return s;
  while (!g_ping_done) {
    if (platform::now_ns() >= deadline) return Status::TimedOut;
    sched::sleep_ns(1'000'000);
  }
  *rtt_ns = platform::now_ns() - start;
  return Status::Ok;
}

void ip_input(const uint8_t* f, uint32_t len) {
  if (len < kEthHeader) return;
  uint16_t type = be16(f + 12);
  const uint8_t* p = f + kEthHeader;
  uint32_t n = len - kEthHeader;
  Config c = config();
  if (type == kEtherArp) {
    if (n < 28 || be16(p) != 1 || be16(p + 2) != kEtherIpv4 || p[4] != 6 || p[5] != 4) return;
    uint32_t spa = get32(p + 14), tpa = get32(p + 24);
    if (spa != 0) arp_learn(spa, p + 8);
    if (be16(p + 6) == kArpRequest && c.ip != 0 && tpa == c.ip) send_arp(kArpReply, p + 8, spa, p + 8);
    return;
  }
  if (type != kEtherIpv4 || n < 20) return;
  uint32_t ihl = (p[0] & 0xf) * 4u;
  uint32_t total = be16(p + 2);
  if ((p[0] >> 4) != 4 || ihl < 20 || total < ihl || total > n) return;
  if (checksum(p, ihl) != 0) return;
  if ((be16(p + 6) & 0x3fff) != 0) return;  // фрагменты не собираем (v1.4)
  uint32_t dst = get32(p + 16), src = get32(p + 12);
  if (dst != c.ip && dst != 0xffffffffu) return;
  const uint8_t* body = p + ihl;
  uint32_t blen = total - ihl;
  if (p[9] == kProtoIcmp && blen >= 8 && checksum(body, blen) == 0) {
    if (body[0] == kIcmpEcho) {
      uint8_t reply[1480];
      if (blen > sizeof reply) return;
      __builtin_memcpy(reply, body, blen);
      reply[0] = kIcmpEchoReply;
      put_be16(reply + 2, 0);
      put_be16(reply + 2, checksum(reply, blen));
      // Без ожидания ARP: поток интерфейса не должен ждать сам себя.
      (void)send_ip(src, kProtoIcmp, reply, blen, 0);
    } else if (body[0] == kIcmpEchoReply && be16(body + 4) == 0x4348 && be16(body + 6) == g_ping_seq &&
               src == g_ping_from) {
      g_ping_done = true;
    }
  }
}

void input(const uint8_t* frame, uint32_t len) { ip_input(frame, len); }

}  // namespace kernel::net
