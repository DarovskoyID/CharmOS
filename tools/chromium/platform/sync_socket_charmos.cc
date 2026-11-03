#include "base/sync_socket.h"
#include "base/check.h"
#include "base/check_op.h"
#include "base/synchronization/lock.h"
#include "base/threading/scoped_blocking_call.h"
#include <charm/stream.hpp>
#include <errno.h>
#include <limits.h>
#include <utility>

namespace base {
struct SyncSocket::NativeState {
  explicit NativeState(Handle endpoint):stream(endpoint) {}
  Lock receive_lock;
  charm::ChannelStream stream;
};
namespace {
uint64_t Deadline(TimeDelta timeout) {
  uint64_t now=charm::now_ns();
  if(timeout.is_max()) return charm::kForever;
  if(timeout.is_negative() || timeout.is_zero()) return now;
  uint64_t delay=static_cast<uint64_t>(timeout.InNanoseconds());
  return delay>=UINT64_MAX-now ? UINT64_MAX-1 : now+delay;
}
size_t ReadUntil(charm::ChannelStream& stream,span<uint8_t> output,uint64_t deadline) {
  size_t received=0;
  while(!output.empty()) {
    auto transfer=stream.read(output.data(),output.size(),deadline);
    if(transfer.status==charm::Status::Interrupted) continue;
    if(transfer.status!=charm::Status::Ok || !transfer.bytes) break;
    received+=transfer.bytes;
    output=output.subspan(transfer.bytes);
  }
  return received;
}
}
const SyncSocket::Handle SyncSocket::kInvalidHandle=0;
SyncSocket::SyncSocket():native_state_(std::make_unique<NativeState>(0)) {}
SyncSocket::SyncSocket(Handle endpoint):handle_(ScopedHandle::adopt(endpoint)),
    native_state_(std::make_unique<NativeState>(endpoint)) {}
SyncSocket::SyncSocket(ScopedHandle endpoint):handle_(std::move(endpoint)),
    native_state_(std::make_unique<NativeState>(handle_.get())) {}
SyncSocket::~SyncSocket()=default;
bool SyncSocket::CreatePair(SyncSocket* first,SyncSocket* second) {
  if(!first || !second || first==second || first->IsValid() || second->IsValid()) {
    errno=EINVAL; return false;
  }
  charm::Handle pair[2];
  if(charm::channel_create(pair)!=charm::Status::Ok) return false;
  first->handle_=ScopedHandle::adopt(pair[0]);
  second->handle_=ScopedHandle::adopt(pair[1]);
  first->native_state_->stream.reset(pair[0]);
  second->native_state_->stream.reset(pair[1]);
  return true;
}
void SyncSocket::Close() { handle_.reset(); native_state_->stream.reset(); }
bool SyncSocket::IsValid() const { return static_cast<bool>(handle_); }
SyncSocket::Handle SyncSocket::handle() const { return handle_.get(); }
SyncSocket::Handle SyncSocket::Release() {
  // A transferred endpoint cannot carry bytes already consumed into this
  // object's local partial-frame buffer. Require a drained, quiescent stream.
  CHECK_EQ(native_state_->stream.buffered_bytes(),0u);
  native_state_->stream.reset();
  return handle_.release();
}
SyncSocket::ScopedHandle SyncSocket::Take() { return ScopedHandle::adopt(Release()); }
size_t SyncSocket::Send(span<const uint8_t> data) {
  CHECK_LE(data.size(),static_cast<size_t>(INT_MAX));
  ScopedBlockingCall blocking(FROM_HERE,BlockingType::MAY_BLOCK);
  auto transferred=native_state_->stream.write(data.data(),data.size());
  return transferred.status==charm::Status::Ok ? transferred.bytes : 0;
}
size_t SyncSocket::Receive(span<uint8_t> output) {
  CHECK_LE(output.size(),static_cast<size_t>(INT_MAX));
  ScopedBlockingCall blocking(FROM_HERE,BlockingType::MAY_BLOCK);
  AutoLock guard(native_state_->receive_lock);
  size_t received=ReadUntil(native_state_->stream,output,charm::kForever);
  return received==output.size() ? received : 0;
}
size_t SyncSocket::ReceiveWithTimeout(span<uint8_t> output,TimeDelta timeout) {
  CHECK_LE(output.size(),static_cast<size_t>(INT_MAX));
  ScopedBlockingCall blocking(FROM_HERE,BlockingType::MAY_BLOCK);
  uint64_t deadline=Deadline(timeout);
  AutoLock guard(native_state_->receive_lock);
  return ReadUntil(native_state_->stream,output,deadline);
}
size_t SyncSocket::Peek() {
  AutoLock guard(native_state_->receive_lock);
  size_t available=0;
  return native_state_->stream.available(&available)==charm::Status::Ok ? available : 0;
}
CancelableSyncSocket::CancelableSyncSocket()=default;
CancelableSyncSocket::CancelableSyncSocket(Handle endpoint):SyncSocket(endpoint) {}
CancelableSyncSocket::CancelableSyncSocket(ScopedHandle endpoint):SyncSocket(std::move(endpoint)) {}
bool CancelableSyncSocket::CreatePair(CancelableSyncSocket* first,CancelableSyncSocket* second) {
  return SyncSocket::CreatePair(first,second);
}
bool CancelableSyncSocket::Shutdown() {
  return native_state_->stream.shutdown()==charm::Status::Ok;
}
size_t CancelableSyncSocket::Send(span<const uint8_t> data) {
  CHECK_LE(data.size(),static_cast<size_t>(INT_MAX));
  auto transferred=native_state_->stream.write(data.data(),data.size(),0);
  return transferred.status==charm::Status::Ok ? transferred.bytes : 0;
}
} // namespace base
