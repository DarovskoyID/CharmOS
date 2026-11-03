#include "build/charmos/native_channel.h"
#include "base/pickle.h"
#include "base/threading/scoped_blocking_call.h"
#include <charm/stream.hpp>
#include <errno.h>
#include <array>

namespace base {
namespace {
ssize_t Failure(charm::Status status) {
  switch(status) {
    case charm::Status::BadHandle: errno=EBADF; break;
    case charm::Status::AccessDenied: errno=EACCES; break;
    case charm::Status::NoMemory: errno=ENOMEM; break;
    case charm::Status::WouldBlock: errno=EAGAIN; break;
    case charm::Status::Interrupted: errno=EINTR; break;
    case charm::Status::BufferTooSmall: errno=EMSGSIZE; break;
    case charm::Status::PeerClosed: errno=EPIPE; break;
    case charm::Status::InvalidArgs: errno=EINVAL; break;
    case charm::Status::Busy: errno=EBUSY; break;
    default: errno=EIO; break;
  }
  return -1;
}
}
bool CreateSocketPair(charm::UniqueHandle* first,charm::UniqueHandle* second) {
  if(!first || !second || first==second || *first || *second) { errno=EINVAL; return false; }
  charm::Handle endpoints[2];
  auto status=charm::channel_create(endpoints);
  if(status!=charm::Status::Ok) { (void)Failure(status); return false; }
  *first=charm::UniqueHandle::adopt(endpoints[0]);
  *second=charm::UniqueHandle::adopt(endpoints[1]);
  return true;
}
bool NativeChannel::EnableReceiveProcessId(charm::Handle) { errno=ENOTSUP; return false; }
bool NativeChannel::SendMsg(charm::Handle endpoint,span<const uint8_t> bytes,
                             const std::vector<charm::Handle>& capabilities) {
  if(capabilities.size()>kMaxFileDescriptors || bytes.size()>abi::syscall::kChannelMaxBytes) {
    errno=EMSGSIZE; return false;
  }
  std::array<charm::UniqueHandle,16> owners;
  std::array<charm::Handle,16> outgoing{};
  for(size_t i=0;i<capabilities.size();++i) {
    auto rights=charm::handle_rights(capabilities[i]);
    if(!rights.ok()) { (void)Failure(rights.status); return false; }
    if(!(rights.value&charm::rights::kTransfer)) { errno=EACCES; return false; }
    auto copy=charm::handle_duplicate(capabilities[i],static_cast<uint32_t>(rights.value));
    if(!copy.ok()) { (void)Failure(copy.status); return false; }
    owners[i]=charm::UniqueHandle::adopt(static_cast<charm::Handle>(copy.value));
    outgoing[i]=owners[i].get();
  }
  ScopedBlockingCall blocking(FROM_HERE,BlockingType::MAY_BLOCK);
  for(;;) {
    auto status=charm::channel_write(endpoint,bytes.data(),bytes.size(),outgoing.data(),capabilities.size());
    if(status==charm::Status::Ok) {
      for(auto& owner:owners) (void)owner.release();
      return true;
    }
    if(status!=charm::Status::WouldBlock) { (void)Failure(status); return false; }
    status=charm::wait_channel(endpoint,2,charm::kForever);
    if(status==charm::Status::Interrupted) continue;
    if(status!=charm::Status::Ok) { (void)Failure(status); return false; }
  }
}
ssize_t NativeChannel::RecvMsg(charm::Handle endpoint,span<uint8_t> bytes,
                               std::vector<charm::UniqueHandle>* capabilities) {
  return RecvMsgWithFlags(endpoint,bytes,0,capabilities,nullptr);
}
ssize_t NativeChannel::RecvMsgWithPid(charm::Handle endpoint,span<uint8_t> bytes,
                                      std::vector<charm::UniqueHandle>* capabilities,ProcessId* pid) {
  return RecvMsgWithFlags(endpoint,bytes,0,capabilities,pid);
}
ssize_t NativeChannel::RecvMsgWithFlags(charm::Handle endpoint,span<uint8_t> bytes,int flags,
                                        std::vector<charm::UniqueHandle>* capabilities,ProcessId* pid) {
  if(pid || (flags&~kDoNotWait)) { errno=ENOTSUP; return -1; }
  if(!capabilities) { errno=EFAULT; return -1; }
  std::array<charm::Handle,16> received{};
  ScopedBlockingCall blocking(FROM_HERE,BlockingType::MAY_BLOCK);
  for(;;) {
    auto incoming=charm::channel_read(endpoint,bytes.data(),bytes.size(),received.data(),received.size(),
                                      (flags&kDoNotWait) ? 0 : charm::kForever);
    if(incoming.status==charm::Status::Interrupted) continue;
    if(incoming.status==charm::Status::PeerClosed) { capabilities->clear(); return 0; }
    if(!incoming.ok()) return Failure(incoming.status);
    std::vector<charm::UniqueHandle> owned;
    size_t count=static_cast<uint32_t>(incoming.value);
    owned.reserve(count);
    for(size_t i=0;i<count;++i) owned.push_back(charm::UniqueHandle::adopt(received[i]));
    *capabilities=std::move(owned);
    return static_cast<ssize_t>(incoming.value>>32);
  }
}
ssize_t NativeChannel::SendRecvMsg(charm::Handle endpoint,span<uint8_t> reply,
                                   charm::Handle* returned,const Pickle& request) {
  return SendRecvMsgWithFlags(endpoint,reply,0,returned,request);
}
ssize_t NativeChannel::SendRecvMsgWithFlags(charm::Handle endpoint,span<uint8_t> reply,int flags,
                                            charm::Handle* returned,const Pickle& request) {
  if(flags&~kDoNotWait) { errno=ENOTSUP; return -1; }
  charm::UniqueHandle receiver,sender;
  if(!CreateSocketPair(&receiver,&sender)) return -1;
  if(!SendMsg(endpoint,request,std::vector<charm::Handle>{sender.get()})) return -1;
  sender.reset();
  std::vector<charm::UniqueHandle> received;
  ssize_t count=RecvMsgWithFlags(receiver.get(),reply,flags,&received,nullptr);
  if(count<0) return count;
  if(received.size()>(returned ? 1u : 0u)) { errno=EMSGSIZE; return -1; }
  if(returned) *returned=received.empty() ? 0 : received[0].release();
  return count;
}
} // namespace base
