#ifndef CHARMOS_CHROMIUM_NATIVE_CHANNEL_H_
#define CHARMOS_CHROMIUM_NATIVE_CHANNEL_H_
#include "base/base_export.h"
#include "base/containers/span.h"
#include "base/process/process_handle.h"
#include <charm/handle.hpp>
#include <sys/types.h>
#include <vector>

namespace base {
class Pickle;
// Native message/capability transport. Handles are kernel capabilities, never
// libc file descriptors, PIDs, Unix paths or SCM_RIGHTS metadata.
bool BASE_EXPORT CreateSocketPair(charm::UniqueHandle* first,charm::UniqueHandle* second);
class BASE_EXPORT NativeChannel {
 public:
  static constexpr size_t kMaxFileDescriptors=16; // Legacy name, native caps.
  static constexpr int kDoNotWait=1;
  static bool EnableReceiveProcessId(charm::Handle endpoint);
  static bool SendMsg(charm::Handle endpoint,span<const uint8_t> bytes,
                      const std::vector<charm::Handle>& capabilities);
  static ssize_t RecvMsg(charm::Handle endpoint,span<uint8_t> bytes,
                         std::vector<charm::UniqueHandle>* capabilities);
  static ssize_t RecvMsgWithPid(charm::Handle endpoint,span<uint8_t> bytes,
                                std::vector<charm::UniqueHandle>* capabilities,ProcessId* pid);
  static ssize_t SendRecvMsg(charm::Handle endpoint,span<uint8_t> reply,
                             charm::Handle* returned,const Pickle& request);
  static ssize_t SendRecvMsgWithFlags(charm::Handle endpoint,span<uint8_t> reply,int flags,
                                      charm::Handle* returned,const Pickle& request);
 private:
  static ssize_t RecvMsgWithFlags(charm::Handle endpoint,span<uint8_t> bytes,int flags,
                                 std::vector<charm::UniqueHandle>* capabilities,ProcessId* pid);
};
// Existing include paths select a typed native API. FD-based consumers must
// explicitly port their capability ownership; integers are not imported as FDs.
using UnixDomainSocket=NativeChannel;
} // namespace base
#endif
