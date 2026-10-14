#pragma once
// UDP (RFC 768) и DHCP-клиент (RFC 2131, опции — RFC 2132).

#include <abi/syscall.hpp>
#include <stdint.h>

namespace kernel::net {

using abi::syscall::Status;

using UdpHandler = void (*)(uint16_t local_port, uint32_t src, uint16_t src_port, const uint8_t* data,
                           uint32_t len);

// Обработчик датаграмм на локальный порт (порт — в порядке хоста).
bool udp_listen(uint16_t port, UdpHandler handler);
void udp_unlisten(uint16_t port);
Status udp_send(uint16_t src_port, uint32_t dst, uint16_t dst_port, const void* data, uint32_t len,
                uint64_t deadline);
void udp_input(uint32_t src, uint32_t dst, const uint8_t* seg, uint32_t len);

// Получает адрес по DHCP (DISCOVER → OFFER → REQUEST → ACK) и вызывает
// configure. Ok — адрес получен до срока.
Status dhcp_configure(uint64_t deadline);

}  // namespace kernel::net
