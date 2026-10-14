#pragma once
// DNS-клиент (RFC 1035): запрос записи A по UDP к серверу из DHCP.

#include <abi/syscall.hpp>
#include <lib/core/string_view.hpp>
#include <stdint.h>

namespace kernel::net {

using abi::syscall::Status;

// IPv4 (порядок сети) имени name. NotFound — имени нет (NXDOMAIN или нет A).
Status dns_resolve(lib::core::StringView name, uint64_t deadline, uint32_t* out);

// Для тестов: сервер DNS и порт вместо данных DHCP (0 — вернуть DHCP и 53).
void dns_set_server(uint32_t ip, uint16_t port);

}  // namespace kernel::net
