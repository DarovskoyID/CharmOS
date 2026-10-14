#include "dns.hpp"

#include "../platform/platform.hpp"
#include "../sched/sched.hpp"
#include "../sync/wait.hpp"
#include "ip.hpp"
#include "udp.hpp"

namespace kernel::net {

namespace {

constexpr uint16_t kLocalPort = 53531;  // свой порт клиента
constexpr uint16_t kTypeA = 1, kClassIn = 1;  // RFC 1035 §3.2.2, §3.2.4

constinit sync::Mutex g_lock{"net.dns"};  // один запрос за раз
constinit uint32_t g_server = 0;
constinit uint16_t g_port = 0;
constinit uint16_t g_id = 0x4348;

struct Answer {
  volatile bool have;
  uint16_t id;
  uint8_t rcode;
  bool found;
  uint32_t ip;
};
constinit Answer g_answer = {};

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }

// Пропускает имя (с указателями сжатия, RFC 1035 §4.1.4); 0 — ошибка.
uint32_t skip_name(const uint8_t* m, uint32_t len, uint32_t pos) {
  for (uint32_t guard = 0; pos < len && guard < 128; ++guard) {
    uint8_t l = m[pos];
    if (l == 0) return pos + 1;
    if ((l & 0xc0) == 0xc0) return pos + 2 <= len ? pos + 2 : 0;
    pos += 1u + l;
  }
  return 0;
}

void handler(uint16_t, uint32_t, uint16_t, const uint8_t* m, uint32_t len) {
  if (len < 12) return;
  uint16_t flags = be16(m + 2);
  if ((flags & 0x8000) == 0) return;  // не ответ (QR)
  Answer a = {};
  a.id = be16(m);
  a.rcode = static_cast<uint8_t>(flags & 0xf);
  uint32_t qd = be16(m + 4), an = be16(m + 6);
  uint32_t pos = 12;
  for (uint32_t i = 0; i < qd && pos != 0; ++i) {
    pos = skip_name(m, len, pos);
    if (pos != 0) pos = pos + 4 <= len ? pos + 4 : 0;
  }
  for (uint32_t i = 0; i < an && pos != 0 && !a.found; ++i) {
    pos = skip_name(m, len, pos);
    if (pos == 0 || pos + 10 > len) break;
    uint16_t type = be16(m + pos), cls = be16(m + pos + 2), rdlen = be16(m + pos + 8);
    pos += 10;
    if (pos + rdlen > len) break;
    if (type == kTypeA && cls == kClassIn && rdlen == 4) {
      __builtin_memcpy(&a.ip, m + pos, 4);
      a.found = true;
    }
    pos += rdlen;
  }
  g_answer.id = a.id;
  g_answer.rcode = a.rcode;
  g_answer.found = a.found;
  g_answer.ip = a.ip;
  __atomic_store_n(&g_answer.have, true, __ATOMIC_RELEASE);
}

}  // namespace

void dns_set_server(uint32_t ip, uint16_t port) {
  sync::MutexGuard guard(g_lock);
  g_server = ip;
  g_port = port;
}

Status dns_resolve(lib::core::StringView name, uint64_t deadline, uint32_t* out) {
  if (name.empty() || name.size() > 253) return Status::InvalidArgs;
  sync::MutexGuard guard(g_lock);
  uint32_t server = g_server != 0 ? g_server : config().dns;
  uint16_t port = g_port != 0 ? g_port : 53;
  if (server == 0 || config().ip == 0) return Status::NetUnreachable;
  // Запрос (RFC 1035 §4.1): заголовок, вопрос A IN, RD = 1.
  uint8_t q[12 + 256 + 4] = {};
  uint16_t id = ++g_id;
  q[0] = static_cast<uint8_t>(id >> 8), q[1] = static_cast<uint8_t>(id);
  q[2] = 0x01;  // RD
  q[5] = 1;     // QDCOUNT
  uint32_t pos = 12;
  for (size_t start = 0; start < name.size();) {
    size_t end = start;
    while (end < name.size() && name[end] != '.') ++end;
    size_t l = end - start;
    if (l == 0 || l > 63) return Status::InvalidArgs;
    q[pos++] = static_cast<uint8_t>(l);
    for (size_t i = start; i < end; ++i) q[pos++] = static_cast<uint8_t>(name[i]);
    start = end + 1;
  }
  q[pos++] = 0;
  q[pos++] = 0, q[pos++] = kTypeA, q[pos++] = 0, q[pos++] = kClassIn;
  if (!udp_listen(kLocalPort, &handler)) return Status::NoMemory;
  Status result = Status::TimedOut;
  for (uint64_t next = 0;;) {
    uint64_t now = platform::now_ns();
    if (now >= deadline) break;
    if (now >= next) {
      __atomic_store_n(&g_answer.have, false, __ATOMIC_RELEASE);
      (void)udp_send(kLocalPort, server, port, q, pos, deadline);
      next = now + 1'000'000'000;
    }
    if (__atomic_load_n(&g_answer.have, __ATOMIC_ACQUIRE) && g_answer.id == id) {
      if (g_answer.rcode == 0 && g_answer.found) {
        *out = g_answer.ip;
        result = Status::Ok;
      } else {
        result = g_answer.rcode == 0 || g_answer.rcode == 3 ? Status::NotFound : Status::IoError;
      }
      break;
    }
    sched::sleep_ns(2'000'000);
  }
  udp_unlisten(kLocalPort);
  return result;
}

}  // namespace kernel::net
