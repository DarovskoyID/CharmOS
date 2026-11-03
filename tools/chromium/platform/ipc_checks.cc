#include "base/sync_socket.h"
#include "build/charmos/native_channel.h"
#include "base/command_line.h"
#include "base/process/launch.h"
#include <charm/libc.h>
#include <charm/memory.hpp>
#include <array>
#include <atomic>
#include <errno.h>
#include <thread>

int TestNativeIpcChild() {
  auto endpoint=charm::UniqueHandle::adopt(charm_take_start_handle(0x40001000u));
  if(!endpoint) return 90;
  std::array<uint8_t,8> request{};
  std::vector<charm::UniqueHandle> received;
  if(base::NativeChannel::RecvMsg(endpoint.get(),request,&received)!=8 || received.size()!=1 || request[0]!=17) return 91;
  auto permissions=charm::handle_rights(received[0].get());
  if(!permissions.ok() || (permissions.value&charm::rights::kWrite)) return 92;
  charm::SharedMemory memory;
  if(charm::SharedMemory::adopt(std::move(received[0]),&memory)!=charm::Status::Ok) return 93;
  charm::Mapping mapping;
  if(memory.map(charm::MemoryAccess::ReadOnly,&mapping)!=charm::Status::Ok ||
     static_cast<const uint8_t*>(mapping.data())[0]!=74) return 94;
  request[0]=81;
  if(!base::NativeChannel::SendMsg(endpoint.get(),request,{memory.handle()})) return 95;
  return 55;
}

int TestNativeIpc() {
  base::SyncSocket first,second;
  if(!base::SyncSocket::CreatePair(&first,&second)) return 100;
  std::array<uint8_t,8> message{1,2,3,4,5,6,7,8},reply{};
  if(first.Send(message)!=message.size() || second.Peek()!=message.size()) return 101;
  if(second.Receive(base::span(reply).first(3u))!=3 || reply[0]!=1 || reply[2]!=3 || second.Peek()!=5) return 102;
  if(second.ReceiveWithTimeout(reply,base::Milliseconds(2))!=5 || reply[0]!=4 || reply[4]!=8 || second.Peek()!=0) return 103;
  auto endpoint=first.Take();
  if(first.IsValid() || !endpoint) return 104;
  base::SyncSocket restored(std::move(endpoint));
  if(restored.Send(message)!=8 || second.Receive(reply)!=8 || reply!=message) return 105;
  restored.Close();
  if(second.ReceiveWithTimeout(reply,base::Milliseconds(2))!=0) return 106;

  base::CancelableSyncSocket writer,reader;
  if(!base::CancelableSyncSocket::CreatePair(&writer,&reader)) return 107;
  for(unsigned i=0;i<abi::syscall::kChannelMaxQueue;++i) if(writer.Send(message)!=8) return 108;
  if(writer.Send(message)!=0 || reader.Receive(reply)!=8 || writer.Send(message)!=8) return 109;
  if(!reader.Shutdown() || writer.Send(message)!=0) return 110;
  base::CancelableSyncSocket cancelled,peer;
  if(!base::CancelableSyncSocket::CreatePair(&cancelled,&peer)) return 111;
  std::atomic<bool> waiting{false};
  size_t cancelled_count=99;
  std::thread blocked([&] {
    std::array<uint8_t,8> blocked_reply{};
    waiting=true;
    cancelled_count=cancelled.Receive(blocked_reply);
  });
  while(!waiting.load()) std::this_thread::yield();
  bool shutdown=cancelled.Shutdown();
  blocked.join();
  if(!shutdown || cancelled_count!=0 || peer.Send(message)!=0) return 112;

  charm::UniqueHandle parent,child_endpoint;
  if(!base::CreateSocketPair(&parent,&child_endpoint)) return 113;
  charm::SharedMemory pixels;
  if(charm::SharedMemory::create(4096,&pixels)!=charm::Status::Ok) return 114;
  charm::Mapping pixels_mapping;
  if(pixels.map(charm::MemoryAccess::ReadWrite,&pixels_mapping)!=charm::Status::Ok) return 115;
  static_cast<uint8_t*>(pixels_mapping.data())[0]=74;
  charm::UniqueHandle readonly_pixels;
  if(pixels.share(charm::rights::kRead|charm::rights::kMap|charm::rights::kDuplicate|
                  charm::rights::kTransfer,&readonly_pixels)!=charm::Status::Ok) return 116;
  message[0]=17;
  if(!base::NativeChannel::SendMsg(parent.get(),message,{readonly_pixels.get()})) return 117;
  std::vector<charm::UniqueHandle> received;
  errno=0;
  if(base::NativeChannel::RecvMsg(child_endpoint.get(),base::span(reply).first(1u),&received)!=-1 ||
     errno!=EMSGSIZE || !received.empty()) return 118;
  base::ProcessId unchanged_pid=1234;
  errno=0;
  if(base::NativeChannel::RecvMsgWithPid(child_endpoint.get(),reply,&received,&unchanged_pid)!=-1 ||
     errno!=ENOTSUP || unchanged_pid!=1234) return 119;
  if(base::NativeChannel::RecvMsg(child_endpoint.get(),reply,&received)!=8 || received.size()!=1 || reply!=message) return 120;
  auto actual_size=charm::vmo_size(received[0].get());
  if(!actual_size.ok() || actual_size.value!=4096 || !charm::handle_rights(readonly_pixels.get()).ok()) return 121;
  received.clear();

  base::LaunchOptions options;
  options.clear_environment=true;
  options.charmos_handles.push_back({child_endpoint.get(),0x40001000u,
      charm::rights::kRead|charm::rights::kWrite|charm::rights::kDuplicate|charm::rights::kTransfer});
  base::CommandLine command(base::FilePath("/bin/base_runtime"));
  command.AppendSwitch("native-channel");
  auto child=base::LaunchProcess(command,options);
  if(!child.IsValid()) return 122;
  child_endpoint.reset();
  if(!base::NativeChannel::SendMsg(parent.get(),message,{readonly_pixels.get()})) return 123;
  if(base::NativeChannel::RecvMsg(parent.get(),reply,&received)!=8 || reply[0]!=81 || received.size()!=1) return 124;
  int code=0;
  if(!child.WaitForExitWithTimeout(base::Seconds(5),&code) || code!=55) return 125;
  if(base::NativeChannel::RecvMsg(parent.get(),reply,&received)!=0 || !received.empty()) return 126;
  return 0;
}
