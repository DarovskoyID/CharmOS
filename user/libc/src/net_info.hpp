#pragma once
#include "internal.hpp"
#include <charm/network.hpp>

namespace libc {
// docs/abi/socket.md, syscall ABI 1.4. Values are in network byte order.
using NetInfo = charm::NetworkInfo;
inline int query_net_info(NetInfo* info) {
  Handle net = start_info().net;
  if (!net) return fail_errno(EACCES);
  auto status = charm::network_info(net, info);
  return status == charm::Status::Ok ? 0 : fail(status);
}
inline bool has_interface(const NetInfo& info) {
  return info.has_interface();
}
}
