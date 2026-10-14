// Порты: однократные подписки на каналы, ожидание со сроком, ошибки.
#include "check.hpp"

using namespace charm;
using abi::syscall::kPacketChannelReadable;
using abi::syscall::kPacketPeerClosed;

extern "C" int64_t charm_main() {
  auto pr = port_create();
  EXPECT(pr.ok());
  Handle port = static_cast<Handle>(pr.value);
  Handle a[2], b[2];
  EXPECT(channel_create(a) == Status::Ok);
  EXPECT(channel_create(b) == Status::Ok);
  PortPacket pk;

  // Пусто: WouldBlock / TimedOut.
  EXPECT(port_wait(port, &pk, 0) == Status::WouldBlock);
  uint64_t deadline = now_ns() + 20'000'000;
  EXPECT(port_wait(port, &pk, deadline) == Status::TimedOut && now_ns() >= deadline);

  // Две подписки — два источника, один порт.
  EXPECT(object_bind(port, a[1], 111) == Status::Ok);
  EXPECT(object_bind(port, b[1], 222) == Status::Ok);
  EXPECT(object_bind(port, a[1], 333) == Status::Busy);  // уже подписан
  EXPECT(send(b[0], "x") == Status::Ok);
  EXPECT(port_wait(port, &pk, kForever) == Status::Ok);
  EXPECT(pk.key == 222 && pk.type == kPacketChannelReadable);
  EXPECT(port_wait(port, &pk, 0) == Status::WouldBlock);  // подписка однократная
  EXPECT(send(b[0], "y") == Status::Ok);
  EXPECT(port_wait(port, &pk, 0) == Status::WouldBlock);  // b не переподписан

  // Подписка на уже готовый канал — пакет сразу.
  EXPECT(object_bind(port, b[1], 444) == Status::Ok);
  EXPECT(port_wait(port, &pk, 0) == Status::Ok && pk.key == 444);

  // Закрытие пира будит подписку на a.
  EXPECT(handle_close(a[0]) == Status::Ok);
  EXPECT(port_wait(port, &pk, kForever) == Status::Ok);
  EXPECT(pk.key == 111 && pk.type == kPacketPeerClosed);

  // Ошибки.
  auto v = vmo_create(4096);
  EXPECT(v.ok());
  EXPECT(object_bind(port, static_cast<Handle>(v.value), 1) == Status::WrongType);
  EXPECT(object_bind(a[1], b[1], 1) == Status::WrongType);
  EXPECT(object_bind(port, 12345, 1) == Status::BadHandle);
  EXPECT(port_wait(a[1], &pk, 0) == Status::WrongType);
  EXPECT(port_wait(port, reinterpret_cast<PortPacket*>(0xffff800000000000ull), 0) ==
         Status::WouldBlock);  // пусто — до копирования не дошло
  EXPECT(object_bind(port, b[1], 5) == Status::Ok);  // b[1] уже готов (y в очереди)
  EXPECT(port_wait(port, reinterpret_cast<PortPacket*>(0xffff800000000000ull), 0) ==
         Status::BadAddress);
  EXPECT(port_wait(port, &pk, 0) == Status::Ok && pk.key == 5);  // пакет не потерян

  // Закрыть порт с активной подпиской: подписка не держит канал, порт освобождается.
  EXPECT(object_bind(port, b[1], 6) == Status::Ok);  // b[1] всё ещё готов → пакет сразу
  Handle c[2];
  EXPECT(channel_create(c) == Status::Ok);
  EXPECT(object_bind(port, c[1], 7) == Status::Ok);
  EXPECT(handle_close(port) == Status::Ok);
  EXPECT(send(c[0], "z") == Status::Ok);  // подписка срабатывает в закрытый порт
  print("port: all checks passed\n");
  return 0;
}
