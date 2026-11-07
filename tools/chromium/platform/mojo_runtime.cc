#include "base/command_line.h"
#include "base/at_exit.h"
#include "base/process/launch.h"
#include "base/time/time.h"
#include "build/charmos/native_channel.h"
#include "mojo/public/cpp/platform/platform_channel.h"
#include <charm/libc.h>
#include <charm/memory.hpp>
#include <charm/stream.hpp>
#include <array>
#include <stdio.h>

bool CheckNativeMojoChannels();
bool CheckNativeMojoIpcz();
int NativeMojoIpczChild();

namespace {
constexpr uint32_t kRoleBase = 0x50000000u;
using Bytes = std::array<uint8_t, 8>;
int Receive(charm::Handle endpoint, Bytes* bytes,
            std::vector<charm::UniqueHandle>* handles) {
  if (charm::wait_channel(endpoint, 1, charm::now_ns() + 5000000000ull) != charm::Status::Ok)
    return -1;
  return static_cast<int>(base::NativeChannel::RecvMsg(endpoint, *bytes, handles));
}
int Child() {
  auto value = base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
      mojo::PlatformChannel::kHandleSwitch);
  auto endpoint = mojo::PlatformChannelEndpoint::RecoverFromString(value);
  if (!endpoint.is_valid() || !endpoint.platform_handle().is_capability()) return 10;
  if (mojo::PlatformChannelEndpoint::RecoverFromString(value).is_valid()) return 11;
  Bytes bytes{};
  std::vector<charm::UniqueHandle> handles;
  auto channel = endpoint.platform_handle().GetCapability().get();
  if (Receive(channel, &bytes, &handles) != 8 || handles.size() != 1 || bytes[0] != 17)
    return 12;
  auto rights = charm::handle_rights(handles[0].get());
  if (!rights.ok() || (rights.value & charm::rights::kWrite)) return 13;
  charm::SharedMemory memory;
  if (charm::SharedMemory::adopt(std::move(handles[0]), &memory) != charm::Status::Ok)
    return 14;
  charm::Mapping mapping;
  if (memory.map(charm::MemoryAccess::ReadOnly, &mapping) != charm::Status::Ok ||
      static_cast<const uint8_t*>(mapping.data())[0] != 74) return 15;
  charm::Mapping forbidden;
  if (memory.map(charm::MemoryAccess::ReadWrite, &forbidden) != charm::Status::AccessDenied)
    return 16;
  bytes[0] = 81;
  if (!base::NativeChannel::SendMsg(channel, bytes, {memory.handle()})) return 17;
  return 37;
}
int Parent() {
  for (const char* invalid : {"", "-1", "1", "4294967296", "1342177280junk"})
    if (mojo::PlatformChannelEndpoint::RecoverFromString(invalid).is_valid()) return 20;
  int initial_fds = charm_open_fd_count();
  for (int attempt = 0; attempt < 2; ++attempt) {
    mojo::PlatformChannel channel;
    auto local = channel.TakeLocalEndpoint();
    auto remote = channel.TakeRemoteEndpoint();
    if (!local.is_valid() || !remote.is_valid() || !local.platform_handle().is_capability())
      return 21;
    charm::SharedMemory memory;
    charm::Mapping mapping;
    if (charm::SharedMemory::create(4096, &memory) != charm::Status::Ok ||
        memory.map(charm::MemoryAccess::ReadWrite, &mapping) != charm::Status::Ok) return 22;
    static_cast<uint8_t*>(mapping.data())[0] = 74;
    charm::UniqueHandle readonly;
    uint32_t rights = charm::rights::kRead | charm::rights::kMap |
                      charm::rights::kTransfer | charm::rights::kDuplicate;
    if (memory.share(rights, &readonly) != charm::Status::Ok) return 23;
    base::LaunchOptions options;
    options.clear_environment = true;
    // An existing native role must not collide with the bootstrap endpoint.
    options.charmos_handles.push_back({readonly.get(), kRoleBase, rights});
    base::CommandLine command(base::FilePath("/bin/mojo_runtime"));
    command.AppendSwitch("native-mojo-child");
    remote.PrepareToPass(options, command);
    if (options.charmos_handles.size() != 2 ||
        options.charmos_handles[1].role != kRoleBase + 1 ||
        command.GetSwitchValueASCII(mojo::PlatformChannel::kHandleSwitch) != "1342177281" ||
        !options.fds_to_remap.empty()) return 24;
    Bytes request{17}, response{};
    auto endpoint = local.platform_handle().GetCapability().get();
    if (!base::NativeChannel::SendMsg(endpoint, request, {readonly.get()})) return 25;
    auto child = base::LaunchProcess(command, options);
    remote.ProcessLaunchAttempted();
    if (!child.IsValid() || remote.is_valid()) return 26;
    std::vector<charm::UniqueHandle> received;
    if (Receive(endpoint, &response, &received) != 8 || response[0] != 81 || received.size() != 1)
      return 27;
    auto returned_rights = charm::handle_rights(received[0].get());
    if (!returned_rights.ok() || (returned_rights.value & charm::rights::kWrite)) return 28;
    int code = 0;
    if (!child.WaitForExitWithTimeout(base::Seconds(5), &code) || code != 37) {
      printf("Native Mojo: child exit %d\n", code);
      return 29;
    }
    received.clear();
    Bytes eof{};
    if (Receive(endpoint, &eof, &received) != 0) return 30;
  }
  if (charm_open_fd_count() != initial_fds) return 31;
  printf("Native Mojo: capability endpoints, one-shot bootstrap, read-only VMO and child restart ok\n");
  return 0;
}
}
extern "C" int main(int argc, char** argv) {
  base::AtExitManager exit_manager;
  if (!base::CommandLine::Init(argc, argv)) return 1;
  if (base::CommandLine::ForCurrentProcess()->HasSwitch("native-mojo-ipcz-child"))
    return NativeMojoIpczChild();
  bool child = base::CommandLine::ForCurrentProcess()->HasSwitch("native-mojo-child");
  int code = child ? Child() : Parent();
  if (!child && !code && !CheckNativeMojoChannels()) code = 40;
  if (!child && !code && !CheckNativeMojoIpcz()) code = 41;
  if (code && !(child && code == 37)) printf("Native Mojo: failed %d\n", code);
  return code;
}
