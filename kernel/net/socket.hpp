#pragma once
// Системные вызовы сокетов (docs/abi/socket.md, v1.4).

#include <abi/syscall.hpp>
#include <stdint.h>

namespace kernel::ipc {
struct Cap;
}

namespace kernel::net {

using abi::syscall::Status;

// Отсоединённая капабилити Net со всеми правами (для стартовых сообщений).
Status net_cap(ipc::Cap** out);

Status sys_socket_open(uint32_t net, uint64_t type, uint64_t* out);
Status sys_socket_bind(uint32_t sock, uint64_t user_addr);
Status sys_socket_listen(uint32_t sock, uint64_t backlog);
Status sys_socket_accept(uint32_t sock, uint64_t user_peer, uint64_t deadline, uint64_t* out);
Status sys_socket_connect(uint32_t sock, uint64_t user_addr, uint64_t deadline);
Status sys_socket_send(uint32_t sock, uint64_t buf, uint64_t len, uint64_t user_to, uint64_t* out);
Status sys_socket_recv(uint32_t sock, uint64_t buf, uint64_t cap, uint64_t user_from, uint64_t deadline,
                       uint64_t* out);
Status sys_socket_shutdown(uint32_t sock, uint64_t how);
Status sys_net_info(uint32_t net, uint64_t user_out);
Status sys_dns_resolve(uint32_t net, uint64_t name, uint64_t len, uint64_t deadline, uint64_t* out);

}  // namespace kernel::net
