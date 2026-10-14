#pragma once
// TCP (RFC 9293): соединения, повторы с RTO по RFC 6298, медленный старт и
// избегание перегрузки по RFC 5681, MSS по RFC 6691. Сегменты вне порядка
// отбрасываются (их доставит повтор) — правило 9, SACK и очередь — позже.
// Соединение — индекс в таблице ядра; сокеты (socket.md) держат его.

#include <abi/syscall.hpp>
#include <stdint.h>

namespace kernel::net {

using abi::syscall::Status;

inline constexpr int kNoConn = -1;

// Все вызовы — из потоков ядра (не из прерываний); ждущие — со сроком.
Status tcp_listen(uint16_t port, uint32_t backlog, int* out);
Status tcp_accept(int listener, uint64_t deadline, int* out, uint32_t* peer_ip, uint16_t* peer_port);
Status tcp_connect(uint32_t ip, uint16_t port, uint64_t deadline, int* out);
// Принимает в буфер отправки сколько влезет (ждёт места до срока).
Status tcp_send(int c, const void* buf, uint32_t len, uint64_t deadline, uint32_t* sent);
// 0 байт и Ok — собеседник закрыл передачу.
Status tcp_recv(int c, void* buf, uint32_t cap, uint64_t deadline, uint32_t* got);
Status tcp_shutdown(int c, bool read, bool write);
// Хендл ушёл: закрыть (FIN) и освободить, когда соединение доведено.
void tcp_release(int c);
// Готовность для портов: есть что читать (или конец, или входящее), есть место.
bool tcp_readable(int c);
bool tcp_writable(int c);
bool tcp_local(int c, uint16_t* port);

void tcp_input(uint32_t src, uint32_t dst, const uint8_t* seg, uint32_t len);

// Для тестов: отбрасывать каждый n-й исходящий и входящий сегмент TCP (0 — нет).
void tcp_set_drop(uint32_t every_n);

}  // namespace kernel::net
