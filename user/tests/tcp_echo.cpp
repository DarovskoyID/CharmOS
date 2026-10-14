// M7: эхо-сервер TCP в ring 3 на сокетах v1.4 (docs/abi/socket.md). Хендл Net
// приходит сообщением "net". Обслуживает соединения по одному (порт 7777),
// проверяет поток (клиент шлёт байты (i * 7 + номер) & 0xff — номер в первом
// байте) и отвечает тем же. После 10 соединений принимает вердикт клиента.
#include "check.hpp"

using namespace charm;

namespace {
uint8_t g_buf[2048];
}

extern "C" int64_t charm_main(Handle start) {
  char msg[8];
  Handle hs[1];
  uint32_t n, nh;
  EXPECT(read_msg(start, msg, sizeof msg, hs, 1, &n, &nh) == Status::Ok);
  EXPECT(same(msg, n, "net") && nh == 1);
  Handle net = hs[0];

  // Права сужаются: без NetListen слушать нельзя.
  auto weak = handle_duplicate(net, rights::kRead | abi::syscall::rights::kNetConnect);
  EXPECT(weak.ok());
  auto ws = socket_open(static_cast<Handle>(weak.value), kSockTcp);
  EXPECT(ws.ok());
  SockAddr any = {0, 7777, 0};
  EXPECT(socket_bind(static_cast<Handle>(ws.value), &any) == Status::Ok);
  EXPECT(socket_listen(static_cast<Handle>(ws.value), 8) == Status::AccessDenied);
  EXPECT(handle_close(static_cast<Handle>(ws.value)) == Status::Ok);

  auto ls = socket_open(net, kSockTcp);
  EXPECT(ls.ok());
  Handle lh = static_cast<Handle>(ls.value);
  EXPECT(socket_bind(lh, &any) == Status::Ok);
  EXPECT(socket_listen(lh, 16) == Status::Ok);
  print("tcp_echo: listening on 7777\n");

  // Эхо-соединения обслуживаются, пока не придёт вердикт клиента — соединение,
  // начинающееся с "VERDICT " (клиент проверил эхо у себя; повторные попытки
  // клиента — обычные эхо-соединения).
  uint64_t total = 0;
  int served = 0;
  char line[128];
  for (;;) {
    SockAddr peer;
    auto c = socket_accept(lh, &peer, now_ns() + 120'000'000'000ull);
    EXPECT(c.ok());
    Handle ch = static_cast<Handle>(c.value);
    uint64_t pos = 0;
    uint8_t tag = 0;
    bool broken = false, verdict = false;
    uint32_t vlen = 0;
    for (;;) {
      auto r = socket_recv(ch, g_buf, sizeof g_buf, now_ns() + 60'000'000'000ull);
      if (!r.ok()) {
        broken = true;
        break;
      }
      if (r.value == 0) break;  // клиент закрыл передачу
      if (pos == 0 && r.value >= 8 && same(reinterpret_cast<char*>(g_buf), 8, "VERDICT ")) verdict = true;
      if (verdict) {
        for (uint64_t i = 0; i < r.value && vlen < sizeof line - 1; ++i) line[vlen++] = static_cast<char>(g_buf[i]);
        pos += r.value;
        continue;
      }
      for (uint64_t i = 0; i < r.value; ++i, ++pos) {
        if (pos == 0) tag = g_buf[0];
        EXPECT(g_buf[i] == static_cast<uint8_t>(pos * 7 + tag));
      }
      auto w = socket_send(ch, g_buf, r.value);
      if (!w.ok() || w.value != r.value) {
        broken = true;
        break;
      }
    }
    if (verdict) {
      EXPECT(handle_close(ch) == Status::Ok);
      char out[192];
      auto s1 = lib::core::format_to(out, sizeof out, "tcp_echo: served {} connections, {} bytes verified\n",
                                     served, total);
      debug_write(s1.data(), s1.size());
      lib::core::StringView v(line + 8, vlen - 8);
      auto s2 = lib::core::format_to(out, sizeof out, "tcp_echo: client says: {}\n", v);
      debug_write(s2.data(), s2.size());
      return v == lib::core::StringView("ok") && served >= 10 ? 0 : 1;
    }
    if (!broken && pos != 0) {
      total += pos;
      ++served;
      EXPECT(socket_shutdown(ch, 2) == Status::Ok);
    }
    EXPECT(handle_close(ch) == Status::Ok);
  }
}
