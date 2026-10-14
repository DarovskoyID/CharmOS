#include "socket.hpp"

#include "../ipc/ipc.hpp"
#include "../mm/heap.hpp"
#include "../proc/uaccess.hpp"
#include "../sched/sched.hpp"
#include "../sync/spinlock.hpp"
#include "ip.hpp"
#include "netif.hpp"
#include "tcp.hpp"

namespace kernel::net {

namespace {

namespace rights = abi::syscall::rights;

constexpr uint64_t kTcp = 1, kUdp = 2;

enum class Kind : uint8_t { Free, New, Listener, Conn };

struct Sock {
  Kind kind;
  uint32_t net_rights;  // права Net, через который сокет открыт
  uint16_t port;        // локальный порт после bind
  int conn;             // tcp-соединение (Listener/Conn)
  bool closing;         // хендл ушёл; соединение отпустит поток опроса
};

constexpr uint32_t kSockets = 128;
constinit sync::Spinlock g_lock{sync::LockRank::Block, "net.sock"};
constinit Sock g_socks[kSockets] = {};
constinit bool g_poller = false;

struct SockAddr {
  uint32_t ipv4;  // порядок сети
  uint16_t port;  // порядок хоста
  uint16_t reserved;
};
static_assert(sizeof(SockAddr) == 8);

struct NetInfo {
  uint32_t ipv4, netmask, gateway, dns;
  uint8_t mac[6];
  uint16_t mtu;
  uint32_t flags;
  uint32_t reserved;
};
static_assert(sizeof(NetInfo) == 32);

int alloc_sock(Kind kind, uint32_t net_rights, int conn) {
  sync::SpinGuard guard(g_lock);
  for (uint32_t i = 0; i < kSockets; ++i) {
    if (g_socks[i].kind == Kind::Free && !g_socks[i].closing) {
      g_socks[i] = {kind, net_rights, 0, conn, false};
      return static_cast<int>(i);
    }
  }
  return -1;
}

Sock get(uint64_t id) {
  sync::SpinGuard guard(g_lock);
  return g_socks[id];
}

void set(uint64_t id, const Sock& s) {
  sync::SpinGuard guard(g_lock);
  g_socks[id] = s;
}

// Хук IPC: вызывается под блокировкой IPC, поэтому только помечает сокет —
// tcp_release (FIN, звонок драйверу через IPC) выполнит поток опроса.
void closed(uint64_t id) {
  sync::SpinGuard guard(g_lock);
  g_socks[id].closing = true;
}

void reap_closed() {
  for (uint32_t i = 0; i < kSockets; ++i) {
    Sock s;
    {
      sync::SpinGuard guard(g_lock);
      if (!g_socks[i].closing) continue;
      s = g_socks[i];
      g_socks[i] = {};
    }
    if (s.kind == Kind::Listener || s.kind == Kind::Conn) tcp_release(s.conn);
  }
}

bool ready(uint64_t id, uint32_t* packet) {
  Sock s = get(id);
  if (s.kind != Kind::Listener && s.kind != Kind::Conn) return false;
  if (tcp_readable(s.conn)) {
    *packet = abi::syscall::kPacketSocketReadable;
    return true;
  }
  if (s.kind == Kind::Conn && tcp_writable(s.conn)) {
    *packet = abi::syscall::kPacketSocketWritable;
    return true;
  }
  return false;
}

void poller(void*) {
  for (;;) {
    sched::sleep_ns(10'000'000);  // опрос готовности (правило 9: события — после замеров)
    ipc::poll_sockets(&ready);
    reap_closed();
  }
}

Status sock_of(uint32_t h, uint32_t need, uint64_t* id, Sock* s) {
  Status st = ipc::get_socket(h, need, id);
  if (st != Status::Ok) return st;
  *s = get(*id);
  return Status::Ok;
}

bool read_addr(uint64_t user, SockAddr* a) {
  return proc::copy_from_user(a, user, sizeof *a) && a->reserved == 0;
}

}  // namespace

Status net_cap(ipc::Cap** out) {
  return ipc::create_net(rights::kDuplicate | rights::kTransfer | rights::kRead | rights::kNetAll, out);
}

Status sys_socket_open(uint32_t net, uint64_t type, uint64_t* out) {
  uint32_t r;
  Status s = ipc::get_net(net, 0, &r);
  if (s != Status::Ok) return s;
  if (type == kUdp) return Status::NotSupported;  // UDP-сокеты — следующий шаг M7
  if (type != kTcp) return Status::InvalidArgs;
  // Поток опроса — один раз; запуск (выделение памяти) — не под g_lock.
  if (!__atomic_exchange_n(&g_poller, true, __ATOMIC_ACQ_REL)) {
    ipc::set_socket_closed_hook(&closed);
    (void)sched::spawn("sockpoll", &poller, nullptr);
  }
  int id = alloc_sock(Kind::New, r, kNoConn);
  if (id < 0) return Status::NoMemory;
  return ipc::install_socket(static_cast<uint64_t>(id), out);
}

Status sys_socket_bind(uint32_t h, uint64_t user_addr) {
  uint64_t id;
  Sock s;
  Status st = sock_of(h, rights::kWrite, &id, &s);
  if (st != Status::Ok) return st;
  SockAddr a;
  if (!read_addr(user_addr, &a)) return Status::BadAddress;
  if (s.kind != Kind::New || s.port != 0 || a.port == 0) return Status::InvalidArgs;
  if (a.ipv4 != 0 && a.ipv4 != config().ip) return Status::NetUnreachable;
  s.port = a.port;
  set(id, s);
  return Status::Ok;
}

Status sys_socket_listen(uint32_t h, uint64_t backlog) {
  uint64_t id;
  Sock s;
  Status st = sock_of(h, rights::kWrite, &id, &s);
  if (st != Status::Ok) return st;
  if (backlog == 0 || backlog > 128) return Status::InvalidArgs;
  if (s.kind != Kind::New || s.port == 0) return Status::InvalidArgs;
  uint32_t need = s.port < 1024 ? rights::kNetListenLow : rights::kNetListen;
  if ((s.net_rights & need) != need) return Status::AccessDenied;
  int c;
  st = tcp_listen(s.port, static_cast<uint32_t>(backlog), &c);
  if (st != Status::Ok) return st;
  s.kind = Kind::Listener;
  s.conn = c;
  set(id, s);
  return Status::Ok;
}

Status sys_socket_accept(uint32_t h, uint64_t user_peer, uint64_t deadline, uint64_t* out) {
  uint64_t id;
  Sock s;
  Status st = sock_of(h, rights::kRead, &id, &s);
  if (st != Status::Ok) return st;
  if (s.kind != Kind::Listener) return Status::InvalidArgs;
  int c;
  uint32_t ip;
  uint16_t port;
  st = tcp_accept(s.conn, deadline, &c, &ip, &port);
  if (st != Status::Ok) return st;
  int nid = alloc_sock(Kind::Conn, s.net_rights, c);
  if (nid < 0) {
    tcp_release(c);
    return Status::NoMemory;
  }
  st = ipc::install_socket(static_cast<uint64_t>(nid), out);
  if (st != Status::Ok) return st;
  if (user_peer != 0) {
    SockAddr a = {ip, port, 0};
    if (!proc::copy_to_user(user_peer, &a, sizeof a)) return Status::BadAddress;
  }
  return Status::Ok;
}

Status sys_socket_connect(uint32_t h, uint64_t user_addr, uint64_t deadline) {
  uint64_t id;
  Sock s;
  Status st = sock_of(h, rights::kWrite, &id, &s);
  if (st != Status::Ok) return st;
  if ((s.net_rights & rights::kNetConnect) == 0) return Status::AccessDenied;
  if (s.kind != Kind::New) return Status::InvalidArgs;
  SockAddr a;
  if (!read_addr(user_addr, &a)) return Status::BadAddress;
  if (config().ip == 0 || !link_up()) return Status::NetUnreachable;
  int c;
  st = tcp_connect(a.ipv4, a.port, deadline, &c);
  if (c != kNoConn) {
    s.conn = c;
    s.kind = Kind::Conn;
    set(id, s);
  }
  return st;
}

Status sys_socket_send(uint32_t h, uint64_t buf, uint64_t len, uint64_t user_to, uint64_t* out) {
  *out = 0;
  uint64_t id;
  Sock s;
  Status st = sock_of(h, rights::kWrite, &id, &s);
  if (st != Status::Ok) return st;
  if (s.kind != Kind::Conn || user_to != 0) return Status::InvalidArgs;
  uint8_t chunk[2048];
  for (uint64_t done = 0; done < len;) {
    uint32_t n = len - done < sizeof chunk ? static_cast<uint32_t>(len - done) : sizeof chunk;
    if (!proc::copy_from_user(chunk, buf + done, n)) return done != 0 ? Status::Ok : Status::BadAddress;
    uint32_t sent;
    st = tcp_send(s.conn, chunk, n, abi::syscall::kInfiniteDeadline, &sent);
    done += sent;
    *out = done;
    if (st != Status::Ok) return done != 0 ? Status::Ok : st;
  }
  return Status::Ok;
}

Status sys_socket_recv(uint32_t h, uint64_t buf, uint64_t cap, uint64_t user_from, uint64_t deadline,
                       uint64_t* out) {
  *out = 0;
  uint64_t id;
  Sock s;
  Status st = sock_of(h, rights::kRead, &id, &s);
  if (st != Status::Ok) return st;
  if (s.kind != Kind::Conn || user_from != 0) return Status::InvalidArgs;
  uint8_t chunk[2048];
  uint32_t n = cap < sizeof chunk ? static_cast<uint32_t>(cap) : sizeof chunk;
  uint32_t got;
  st = tcp_recv(s.conn, chunk, n, deadline, &got);
  if (st != Status::Ok) return st;
  if (got != 0 && !proc::copy_to_user(buf, chunk, got)) return Status::BadAddress;
  *out = got;
  return Status::Ok;
}

Status sys_socket_shutdown(uint32_t h, uint64_t how) {
  uint64_t id;
  Sock s;
  Status st = sock_of(h, rights::kWrite, &id, &s);
  if (st != Status::Ok) return st;
  if (how == 0 || how > 3 || s.kind != Kind::Conn) return Status::InvalidArgs;
  return tcp_shutdown(s.conn, (how & 1) != 0, (how & 2) != 0);
}

Status sys_net_info(uint32_t net, uint64_t user_out) {
  uint32_t r;
  Status st = ipc::get_net(net, 0, &r);
  if (st != Status::Ok) return st;
  Config c = config();
  NetInfo info = {c.ip, c.netmask, c.gateway, c.dns, {}, mtu(), link_up() ? 1u : 0u, 0};
  (void)mac(info.mac);
  return proc::copy_to_user(user_out, &info, sizeof info) ? Status::Ok : Status::BadAddress;
}

Status sys_dns_resolve(uint32_t net, uint64_t, uint64_t, uint64_t, uint64_t*) {
  uint32_t r;
  Status st = ipc::get_net(net, rights::kNetResolve, &r);
  if (st != Status::Ok) return st;
  return Status::NotSupported;  // DNS-клиент — следующий шаг M7
}

}  // namespace kernel::net
