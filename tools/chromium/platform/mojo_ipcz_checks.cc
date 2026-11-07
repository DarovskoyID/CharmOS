// End-to-end Mojo public API test: two real processes, not a local mock node.
#include "base/command_line.h"
#include "base/memory/read_only_shared_memory_region.h"
#include "base/process/launch.h"
#include "base/threading/platform_thread.h"
#include "base/threading/thread.h"
#include "base/time/time.h"
#include "mojo/core/embedder/embedder.h"
#include "mojo/core/embedder/scoped_ipc_support.h"
#include "mojo/public/cpp/platform/platform_channel.h"
#include "mojo/public/cpp/system/invitation.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "mojo/public/cpp/system/platform_handle.h"
#include <charm/handle.hpp>
#include <array>
#include <stdio.h>
#include <string.h>

namespace {
class Runtime {
 public:
  explicit Runtime(bool broker) : io_("NativeMojoIO") {
    if (!io_.StartWithOptions(base::Thread::Options(base::MessagePumpType::IO, 0))) return;
    support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
        io_.task_runner(), mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);
    mojo::core::Configuration configuration;
    configuration.is_broker_process = broker;
    // Native processes can allocate their own VMO-backed memory. This uses
    // actual kernel allocation, without a platform shared-memory broker.
    configuration.force_direct_shared_memory_allocation = true;
    mojo::core::Init(configuration);
    valid_ = true;
  }
  ~Runtime() {
    if (valid_) mojo::core::ShutDown();
    io_.Stop();
  }
  bool valid() const { return valid_; }
 private:
  base::Thread io_;
  std::unique_ptr<mojo::core::ScopedIPCSupport> support_;
  bool valid_ = false;
};
bool Receive(mojo::MessagePipeHandle pipe, std::vector<uint8_t>* bytes,
             std::vector<mojo::ScopedHandle>* handles) {
  auto deadline = base::TimeTicks::Now() + base::Seconds(8);
  for (;;) {
    auto result = mojo::ReadMessageRaw(pipe, bytes, handles, MOJO_READ_MESSAGE_FLAG_NONE);
    if (result == MOJO_RESULT_OK) return true;
    if (result != MOJO_RESULT_SHOULD_WAIT || base::TimeTicks::Now() >= deadline) return false;
    // Only the test waits here. Production transport uses native readiness on
    // the separate Mojo I/O thread, which remains free to process the message.
    base::PlatformThread::Sleep(base::Milliseconds(1));
  }
}
bool Send(mojo::MessagePipeHandle pipe, base::span<const uint8_t> bytes,
          base::ReadOnlySharedMemoryRegion region) {
  auto buffer = mojo::WrapReadOnlySharedMemoryRegion(std::move(region));
  if (!buffer.is_valid()) return false;
  MojoHandle handle = buffer.get().value();
  auto result = mojo::WriteMessageRaw(pipe, bytes.data(), bytes.size(), &handle, 1,
                                     MOJO_WRITE_MESSAGE_FLAG_NONE);
  if (result != MOJO_RESULT_OK) return false;
  (void)buffer.release();
  return true;
}
base::ReadOnlySharedMemoryRegion Unwrap(std::vector<mojo::ScopedHandle>* handles) {
  if (handles->size() != 1) return {};
  return mojo::UnwrapReadOnlySharedMemoryRegion(mojo::ScopedSharedBufferHandle(
      mojo::SharedBufferHandle((*handles)[0].release().value())));
}
bool ReadOnlyValue(const base::ReadOnlySharedMemoryRegion& region, uint8_t expected) {
  if (!region.IsValid()) return false;
  auto rights = charm::handle_rights(region.GetPlatformHandle());
  if (!rights.ok() || (rights.value & charm::rights::kWrite)) return false;
  auto mapping = region.Map();
  return mapping.IsValid() && static_cast<const uint8_t*>(mapping.memory())[0] == expected;
}
}
int NativeMojoIpczChild() {
  Runtime runtime(false);
  if (!runtime.valid()) return 50;
  auto value = base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
      mojo::PlatformChannel::kHandleSwitch);
  auto endpoint = mojo::PlatformChannelEndpoint::RecoverFromString(value);
  if (!endpoint.is_valid()) return 51;
  auto invitation = mojo::IncomingInvitation::Accept(std::move(endpoint));
  auto pipe = invitation.ExtractMessagePipe("native-test");
  if (!pipe.is_valid()) return 52;
  std::vector<uint8_t> bytes;
  std::vector<mojo::ScopedHandle> handles;
  if (!Receive(pipe.get(), &bytes, &handles) || bytes.size() != 512 * 1024 + 31) return 53;
  for (size_t i = 0; i < bytes.size(); ++i)
    if (bytes[i] != static_cast<uint8_t>(i * 13 + i / 65504)) return 54;
  auto region = Unwrap(&handles);
  if (!ReadOnlyValue(region, 41)) return 55;
  const std::array<uint8_t, 8> reply{81,2,3,4,5,6,7,8};
  if (!Send(pipe.get(), reply, std::move(region))) return 56;
  // Keep the sender alive until the actual remote node acknowledges receipt.
  bytes.clear(); handles.clear();
  if (!Receive(pipe.get(), &bytes, &handles) || bytes.size() != 1 || bytes[0] != 99 ||
      !handles.empty()) return 57;
  return 37;
}
bool CheckNativeMojoIpcz() {
  Runtime runtime(true);
  if (!runtime.valid()) return false;
  for (unsigned restart = 0; restart < 2; ++restart) {
    mojo::PlatformChannel channel;
    auto local = channel.TakeLocalEndpoint();
    auto remote = channel.TakeRemoteEndpoint();
    base::LaunchOptions options;
    options.clear_environment = true;
    base::CommandLine command(base::FilePath("/bin/mojo_runtime"));
    command.AppendSwitch("native-mojo-ipcz-child");
    remote.PrepareToPass(options, command);
    mojo::OutgoingInvitation invitation;
    auto pipe = invitation.AttachMessagePipe("native-test");
    auto child = base::LaunchProcess(command, options);
    remote.ProcessLaunchAttempted();
    if (!child.IsValid() || !pipe.is_valid()) return false;
    mojo::OutgoingInvitation::Send(std::move(invitation), child.Handle(), std::move(local));
    auto memory = base::ReadOnlySharedMemoryRegion::Create(4096);
    if (!memory.IsValid()) { child.Terminate(1, true); return false; }
    static_cast<uint8_t*>(memory.mapping.memory())[0] = 41;
    std::vector<uint8_t> payload(512 * 1024 + 31);
    for (size_t i = 0; i < payload.size(); ++i)
      payload[i] = static_cast<uint8_t>(i * 13 + i / 65504);
    if (!Send(pipe.get(), payload, std::move(memory.region))) {
      child.Terminate(1, true); return false;
    }
    std::vector<uint8_t> bytes;
    std::vector<mojo::ScopedHandle> handles;
    if (!Receive(pipe.get(), &bytes, &handles) || bytes.size() != 8 || bytes[0] != 81 ||
        !ReadOnlyValue(Unwrap(&handles), 41)) {
      child.Terminate(1, true); return false;
    }
    const uint8_t ack = 99;
    if (mojo::WriteMessageRaw(pipe.get(), &ack, 1, nullptr, 0, MOJO_WRITE_MESSAGE_FLAG_NONE) != MOJO_RESULT_OK) {
      child.Terminate(1, true); return false;
    }
    int code = 0;
    if (!child.WaitForExitWithTimeout(base::Seconds(8), &code) || code != 37) {
      printf("Native Mojo: ipcz child exit %d\n", code);
      child.Terminate(1, true); return false;
    }
  }
  printf("Native Mojo: public invitation, message pipe, shared-buffer transfer and process restart ok\n");
  return true;
}
