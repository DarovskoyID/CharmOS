#pragma once
// IPv4 поверх Ethernet: ARP (RFC 826), IPv4 (RFC 791, 894), ICMP эхо
// (RFC 792). Адреса — в порядке байт сети, упакованные в uint32_t как на
// проводе (первый октет — младший байт в памяти).

#include <abi/syscall.hpp>
#include <stdint.h>

namespace kernel::net {

using abi::syscall::Status;

struct Config {
  uint32_t ip, netmask, gateway, dns;
};

void configure(const Config& c);
Config config();

constexpr uint32_t ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return uint32_t{a} | uint32_t{b} << 8 | uint32_t{c} << 16 | uint32_t{d} << 24;
}

// Отправка IP-датаграммы с протоколом proto (через шлюз, если адрес вне сети).
Status send_ip(uint32_t dst, uint8_t proto, const void* payload, uint32_t len, uint64_t deadline);

// Эхо-запрос ICMP; Ok — ответ пришёл до срока, *rtt_ns — время.
Status ping(uint32_t dst, uint16_t seq, uint64_t deadline, uint64_t* rtt_ns);

// Обработка IPv4/ARP-кадров (вызывает netif::input).
void ip_input(const uint8_t* frame, uint32_t len);

uint16_t checksum(const void* data, uint32_t len, uint32_t initial = 0);

}  // namespace kernel::net
