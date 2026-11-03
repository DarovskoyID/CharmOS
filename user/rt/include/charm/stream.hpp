#pragma once
#include <charm/handle.hpp>
#include <stddef.h>

namespace charm {
struct StreamTransfer { Status status; size_t bytes; };
inline Status wait_channel(Handle endpoint,uint32_t interest,uint64_t deadline) {
  auto created=port_create();
  if(!created.ok()) return created.status;
  auto port=UniqueHandle::adopt(static_cast<Handle>(created.value));
  auto status=object_watch(port.get(),endpoint,1,interest);
  if(status!=Status::Ok) return status;
  struct Subscription {
    Handle port,endpoint;
    ~Subscription() { (void)object_unbind(port,endpoint,1); }
  } subscription{port.get(),endpoint};
  PortPacket packet{};
  status=port_wait(port.get(),&packet,deadline);
  if(status!=Status::Ok) return status;
  if(packet.key!=1 || (packet.type!=abi::syscall::kPacketChannelWritable &&
                      packet.type!=abi::syscall::kPacketChannelReadable &&
                      packet.type!=abi::syscall::kPacketPeerClosed)) return Status::IoError;
  return Status::Ok;
}

// Borrowed endpoint, one reader and one writer; reset/destruction require
// quiescent I/O. shutdown() may run concurrently and never changes the handle.
// Both peers must use this framing: payload-only records of at most 4096 bytes.
class ChannelStream {
 public:
  explicit ChannelStream(Handle endpoint=0):endpoint_(endpoint) {}
  ChannelStream(const ChannelStream&)=delete;
  ChannelStream& operator=(const ChannelStream&)=delete;
  void reset(Handle endpoint=0) { endpoint_=endpoint; position_=length_=0; }
  Handle handle() const { return endpoint_; }
  size_t buffered_bytes() const { return length_-position_; }
  Status shutdown(uint32_t directions=3) const { return channel_shutdown(endpoint_,directions); }

  Status available(size_t* output) const {
    if(!output) return Status::InvalidArgs;
    abi::syscall::ChannelState state{};
    auto status=channel_query(endpoint_,&state);
    if(status!=Status::Ok) return status;
    *output=(state.flags&1u) ? 0 : static_cast<size_t>(state.queued_bytes)+buffered_bytes();
    return Status::Ok;
  }

  StreamTransfer read(void* output,size_t capacity,uint64_t deadline=kForever) {
    if(!capacity) return {Status::Ok,0};
    if(!output) return {Status::InvalidArgs,0};
    abi::syscall::ChannelState state{};
    auto status=channel_query(endpoint_,&state);
    if(status!=Status::Ok) return {status,0};
    if(state.flags&1u) { position_=length_=0; return {Status::Ok,0}; }
    while(position_==length_) {
      auto incoming=channel_read(endpoint_,buffer_,sizeof buffer_,nullptr,0,deadline);
      if(incoming.status==Status::PeerClosed) return {Status::Ok,0};
      if(!incoming.ok()) return {incoming.status,0};
      length_=static_cast<size_t>(incoming.value>>32); position_=0;
      if(length_) break;
      if(deadline && deadline!=kForever && now_ns()>=deadline) return {Status::TimedOut,0};
    }
    size_t count=capacity<buffered_bytes() ? capacity : buffered_bytes();
    auto* target=static_cast<unsigned char*>(output);
    for(size_t i=0;i<count;++i) target[i]=buffer_[position_+i];
    position_+=count;
    return {Status::Ok,count};
  }

  StreamTransfer write(const void* input,size_t size,uint64_t deadline=kForever) const {
    if(!input && size) return {Status::InvalidArgs,0};
    const auto* source=static_cast<const unsigned char*>(input);
    size_t sent=0;
    while(sent<size) {
      size_t count=size-sent<sizeof buffer_ ? size-sent : sizeof buffer_;
      auto status=channel_write(endpoint_,source+sent,count,nullptr,0);
      if(status==Status::Ok) { sent+=count; continue; }
      if(status!=Status::WouldBlock || !deadline) return {status,sent};
      if(deadline!=kForever && now_ns()>=deadline) return {Status::TimedOut,sent};
      status=wait_channel(endpoint_,2,deadline);
      if(status!=Status::Ok) return {status,sent};
    }
    return {Status::Ok,sent};
  }
 private:
  Handle endpoint_;
  size_t position_=0,length_=0;
  unsigned char buffer_[4096];
};
} // namespace charm
