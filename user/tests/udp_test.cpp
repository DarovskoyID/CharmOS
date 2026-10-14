// M7: UDP-сокеты и dns_resolve из ring 3 (docs/abi/socket.md). На хосте —
// UDP-эхо 127.0.0.1:7779 и DNS-заглушка с записью charm.test → 10.1.2.3
// (tools/qemu/udp.sh); хост виден гостю как 10.0.2.2.
#include "check.hpp"

using namespace charm;

extern "C" int64_t charm_main(Handle start) {
  char msg[8];
  Handle hs[1];
  uint32_t n, nh;
  EXPECT(read_msg(start, msg, sizeof msg, hs, 1, &n, &nh) == Status::Ok);
  EXPECT(same(msg, n, "net") && nh == 1);
  Handle net = hs[0];

  // DNS: есть имя, нет имени, нет права.
  const char name[] = "charm.test";
  auto r = syscall5(Number::DnsResolve, net, reinterpret_cast<uint64_t>(name), sizeof name - 1,
                    now_ns() + 5'000'000'000ull, 0);
  EXPECT(r.ok() && r.value == (10u | 1u << 8 | 2u << 16 | 3u << 24));
  const char missing[] = "nothing.test";
  r = syscall5(Number::DnsResolve, net, reinterpret_cast<uint64_t>(missing), sizeof missing - 1,
               now_ns() + 5'000'000'000ull, 0);
  EXPECT(r.status == Status::NotFound);
  auto weak = handle_duplicate(net, rights::kRead | abi::syscall::rights::kNetConnect);
  EXPECT(weak.ok());
  r = syscall5(Number::DnsResolve, static_cast<Handle>(weak.value), reinterpret_cast<uint64_t>(name),
               sizeof name - 1, now_ns() + 5'000'000'000ull, 0);
  EXPECT(r.status == Status::AccessDenied);
  print("udp_test: dns ok\n");

  // UDP: датаграммы к эхо-серверу и обратно, с адресом отправителя.
  auto s = socket_open(net, 2);
  EXPECT(s.ok());
  Handle sh = static_cast<Handle>(s.value);
  SockAddr echo = {10u | 0u << 8 | 2u << 16 | 2u << 24, 7779, 0};  // 10.0.2.2
  uint8_t out[1000], in[1472];
  int answered = 0;
  for (uint32_t i = 0; i < 20; ++i) {
    for (uint32_t k = 0; k < sizeof out; ++k) out[k] = static_cast<uint8_t>(k + i);
    auto w = syscall5(Number::SocketSend, sh, reinterpret_cast<uint64_t>(out), sizeof out,
                      reinterpret_cast<uint64_t>(&echo), 0);
    EXPECT(w.ok() && w.value == sizeof out);
    SockAddr from;
    auto rr = syscall5(Number::SocketRecv, sh, reinterpret_cast<uint64_t>(in), sizeof in,
                       reinterpret_cast<uint64_t>(&from), now_ns() + 1'000'000'000ull);
    if (rr.status == Status::TimedOut) continue;  // UDP: потеря допустима
    EXPECT(rr.ok() && rr.value == sizeof out && from.ipv4 == echo.ipv4 && from.port == 7779);
    for (uint32_t k = 0; k < sizeof out; ++k) EXPECT(in[k] == out[k]);
    ++answered;
  }
  EXPECT(answered >= 18);
  print("udp_test: udp echo ok\n");
  return 0;
}
