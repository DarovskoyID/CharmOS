// Каналы и VMO в пределах одного процесса: каждая ошибка из docs/abi/ipc.md.
#include "check.hpp"

using namespace charm;

extern "C" int64_t charm_main() {
  auto ver = abi_version();
  EXPECT(ver.ok() && ver.value == ((1u << 16) | 1u));

  Handle ch[2];
  EXPECT(channel_create(ch) == Status::Ok);
  Handle x = ch[0], y = ch[1];
  char buf[256];
  Handle hs[20];

  // Обмен и порядок.
  EXPECT(send(x, "one") == Status::Ok);
  EXPECT(send(x, "two") == Status::Ok);
  auto r = channel_read(y, buf, sizeof buf, hs, 20, kForever);
  EXPECT(r.ok() && r.value == (3ull << 32) && same(buf, 3, "one"));
  r = channel_read(y, buf, sizeof buf, hs, 20, kForever);
  EXPECT(r.ok() && same(buf, 3, "two"));

  // Пусто: не ждать — WouldBlock; со сроком — TimedOut не раньше срока.
  EXPECT(channel_read(y, buf, sizeof buf, hs, 20, 0).status == Status::WouldBlock);
  uint64_t deadline = now_ns() + 20'000'000;
  EXPECT(channel_read(y, buf, sizeof buf, hs, 20, deadline).status == Status::TimedOut);
  EXPECT(now_ns() >= deadline);

  // Мал буфер: сообщение остаётся, в RDX — нужные размеры.
  static char big[100];
  EXPECT(channel_write(x, big, sizeof big, nullptr, 0) == Status::Ok);
  r = channel_read(y, buf, 10, hs, 20, kForever);
  EXPECT(r.status == Status::BufferTooSmall && r.value == (100ull << 32));
  r = channel_read(y, buf, sizeof buf, hs, 20, kForever);
  EXPECT(r.ok() && (r.value >> 32) == 100);

  // Перемещение хендла.
  auto v = vmo_create(8192);
  EXPECT(v.ok());
  Handle vmo = static_cast<Handle>(v.value);
  EXPECT(channel_write(x, "h", 1, &vmo, 1) == Status::Ok);
  EXPECT(handle_close(vmo) == Status::BadHandle);
  r = channel_read(y, buf, sizeof buf, hs, 20, kForever);
  EXPECT(r.ok() && (r.value & 0xffffffff) == 1);
  vmo = hs[0];
  auto m = vmo_map(vmo, rights::kRead | rights::kWrite);
  EXPECT(m.ok());
  reinterpret_cast<volatile char*>(m.value)[8191] = 1;  // вся VMO отображена
  EXPECT(vmo_unmap(m.value) == Status::Ok);
  EXPECT(vmo_unmap(m.value) == Status::InvalidArgs);

  // Без права Transfer не отправить; сообщение атомарно — хендл остался.
  auto d = handle_duplicate(vmo, rights::kRead | rights::kMap);
  EXPECT(d.ok());
  Handle no_transfer = static_cast<Handle>(d.value);
  Handle pair2[2] = {vmo, no_transfer};
  EXPECT(channel_write(x, "z", 1, pair2, 2) == Status::AccessDenied);
  EXPECT(vmo_map(vmo, rights::kRead).ok());  // vmo не ушёл вместе с отказом
  EXPECT(handle_duplicate(no_transfer, rights::kRead).status == Status::AccessDenied);
  EXPECT(handle_duplicate(vmo, rights::kAll | (1u << 7)).status == Status::InvalidArgs);

  // Свой конец и повтор хендла — InvalidArgs; лимиты сообщения.
  EXPECT(channel_write(x, "s", 1, &x, 1) == Status::InvalidArgs);
  Handle twice[2] = {vmo, vmo};
  EXPECT(channel_write(x, "t", 1, twice, 2) == Status::InvalidArgs);
  EXPECT(channel_write(x, big, abi::syscall::kChannelMaxBytes + 1, nullptr, 0) ==
         Status::InvalidArgs);
  EXPECT(channel_write(x, "u", 1, hs, 17) == Status::InvalidArgs);
  EXPECT(channel_write(x, reinterpret_cast<const void*>(0xffff800000000000ull), 4, nullptr, 0) ==
         Status::BadAddress);

  // Очередь — не больше 64 сообщений.
  for (int i = 0; i < 64; ++i) EXPECT(send(x, "q") == Status::Ok);
  EXPECT(send(x, "q") == Status::WouldBlock);
  for (int i = 0; i < 64; ++i) EXPECT(channel_read(y, buf, sizeof buf, hs, 20, 0).ok());

  // Отзыв хендла, лежащего в непрочитанном сообщении: байты доходят, хендл — нет.
  auto d2 = handle_duplicate(vmo, rights::kRead | rights::kMap | rights::kTransfer);
  EXPECT(d2.ok());
  Handle in_flight = static_cast<Handle>(d2.value);
  EXPECT(channel_write(x, "rv", 2, &in_flight, 1) == Status::Ok);
  auto rv = handle_revoke(vmo);
  EXPECT(rv.ok() && rv.value == 2);  // no_transfer + in_flight
  r = channel_read(y, buf, sizeof buf, hs, 20, kForever);
  EXPECT(r.ok() && r.value == (2ull << 32));
  EXPECT(handle_close(no_transfer) == Status::BadHandle);

  // Закрытие: сначала доставляется очередь, потом PeerClosed.
  EXPECT(send(x, "last") == Status::Ok);
  EXPECT(handle_close(x) == Status::Ok);
  r = channel_read(y, buf, sizeof buf, hs, 20, kForever);
  EXPECT(r.ok() && same(buf, 4, "last"));
  EXPECT(channel_read(y, buf, sizeof buf, hs, 20, kForever).status == Status::PeerClosed);
  EXPECT(send(y, "x") == Status::PeerClosed);
  EXPECT(handle_close(y) == Status::Ok);
  EXPECT(handle_close(vmo) == Status::Ok);
  EXPECT(vmo_create(4097).status == Status::InvalidArgs);
  EXPECT(vmo_create(0).status == Status::InvalidArgs);

  print("chan: all checks passed\n");
  return 0;
}
