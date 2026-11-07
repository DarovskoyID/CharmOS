#include "mojo/core/channel.h"
#include "mojo/core/ipcz_driver/envelope.h"
#include "mojo/public/cpp/platform/platform_channel.h"
#include "base/functional/bind.h"
#include "base/memory/platform_shared_memory_region.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/single_thread_task_runner.h"
#include "base/timer/timer.h"
#include "third_party/abseil-cpp/absl/synchronization/mutex.h"
#include "third_party/abseil-cpp/absl/time/time.h"
#include <charm/libc.h>
#include <charm/memory.hpp>
#include <array>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <atomic>
#include <chrono>

namespace {
using mojo::core::Channel;
struct Received {
  std::vector<uint8_t> bytes;
  std::vector<mojo::PlatformHandle> handles;
};
struct Receiver : Channel::Delegate {
  explicit Receiver(bool for_ipcz = false) : ipcz(for_ipcz) {}
  bool IsIpczTransport() const override { return ipcz; }
  void OnChannelMessage(const void* data, size_t size,
                        std::vector<mojo::PlatformHandle> handles,
                        scoped_refptr<mojo::core::ipcz_driver::Envelope>) override {
    Received message;
    if (size) {
      const auto* bytes = static_cast<const uint8_t*>(data);
      message.bytes.assign(bytes, bytes + size);
    }
    message.handles = std::move(handles);
    messages.push_back(std::move(message));
    if (wake) wake.Run();
  }
  void OnChannelError(Channel::Error value) override {
    ++errors; error = value;
    if (wake) wake.Run();
  }
  void OnChannelDestroyed() override { ++destroyed; }
  bool ipcz;
  unsigned errors = 0, destroyed = 0;
  Channel::Error error = Channel::Error::kDisconnected;
  std::vector<Received> messages;
  base::RepeatingClosure wake;
};
template <typename Predicate>
bool Wait(Receiver& first, Receiver& second, Predicate ready) {
  if (ready()) return true;
  base::RunLoop loop;
  auto check = base::BindRepeating(
      [](Receiver* a, Receiver* b, base::RunLoop* running, Predicate* predicate) {
        if ((*predicate)() || a->errors || b->errors) running->Quit();
      }, base::Unretained(&first), base::Unretained(&second),
      base::Unretained(&loop), base::Unretained(&ready));
  first.wake = second.wake = check;
  base::OneShotTimer timeout;
  timeout.Start(FROM_HERE, base::Seconds(8), loop.QuitClosure());
  loop.Run();
  first.wake.Reset(); second.wake.Reset();
  return ready();
}
struct Pair {
  explicit Pair(bool ipcz = false,
                Channel::HandlePolicy policy = Channel::HandlePolicy::kAcceptHandles)
      : first(ipcz), second(ipcz) {
    mojo::PlatformChannel endpoints;
    auto local = endpoints.TakeLocalEndpoint();
    auto remote = endpoints.TakeRemoteEndpoint();
    raw_local = local.platform_handle().GetCapability().get();
    raw_remote = remote.platform_handle().GetCapability().get();
    auto runner = base::SingleThreadTaskRunner::GetCurrentDefault();
    a = Channel::Create(&first, mojo::core::ConnectionParams(std::move(local)), policy, runner);
    b = Channel::Create(&second, mojo::core::ConnectionParams(std::move(remote)), policy, runner);
  }
  ~Pair() {
    a->ShutDown(); b->ShutDown();
    base::RunLoop().RunUntilIdle();
    a = nullptr; b = nullptr;
    base::RunLoop().RunUntilIdle();
  }
  Receiver first, second;
  scoped_refptr<Channel> a, b;
  charm::Handle raw_local = 0, raw_remote = 0;
};
bool Synchronization() {
  absl::Mutex mutex;
  std::atomic<bool> waiting{false};
  int value = 0;
  mutex.Lock();
  std::thread blocked([&] {
    waiting.store(true, std::memory_order_release);
    mutex.Lock();
    value = 1;
    mutex.Unlock();
  });
  while (!waiting.load(std::memory_order_acquire)) std::this_thread::yield();
  std::this_thread::sleep_for(std::chrono::milliseconds(3));
  mutex.Unlock();
  blocked.join();
  if (value != 1) return false;
  bool condition = false;
  mutex.Lock();
  bool signaled = mutex.AwaitWithTimeout(absl::Condition(&condition), absl::Milliseconds(2));
  mutex.Unlock();
  return !signaled;
}
bool HandleOwnership() {
  charm::SharedMemory memory;
  if (charm::SharedMemory::create(4096, &memory) != charm::Status::Ok) return false;
  charm::UniqueHandle native;
  if (memory.share(charm::rights::kRead | charm::rights::kMap | charm::rights::kTransfer |
                   charm::rights::kDuplicate, &native) != charm::Status::Ok) return false;
  mojo::PlatformHandle original(std::move(native));
  auto clone = original.Clone();
  if (!clone.is_capability() || !clone.is_valid()) return false;
  auto& same = original;
  original = std::move(same); // Ownership-preserving self move.
  if (!original.is_valid()) return false;
  MojoPlatformHandle wire{};
  mojo::PlatformHandle::ToMojoPlatformHandle(std::move(clone), &wire);
  if (wire.type != MOJO_PLATFORM_HANDLE_TYPE_CHARMOS_CAPABILITY || !wire.value || clone.is_valid())
    return false;
  auto restored = mojo::PlatformHandle::FromMojoPlatformHandle(&wire);
  if (!restored.is_capability() || !restored.is_valid()) return false;
  wire.value = UINT64_MAX;
  if (mojo::PlatformHandle::FromMojoPlatformHandle(&wire).is_valid()) return false;
  wire.type = MOJO_PLATFORM_HANDLE_TYPE_FILE_DESCRIPTOR;
  wire.value = uint64_t{1} << 32;
  if (mojo::PlatformHandle::FromMojoPlatformHandle(&wire).is_valid()) return false;
  using Region = base::subtle::PlatformSharedMemoryRegion;
  auto guid = base::UnguessableToken::Create();
  if (Region::TakeOrFail({}, Region::Mode::kReadOnly, 4096, guid).has_value()) return false;
  if (Region::TakeOrFail(restored.TakeCapability(), Region::Mode::kReadOnly, 0, guid).has_value())
    return false;
  return original.is_valid();
}
bool Transfer(bool ipcz) {
  Pair pair(ipcz);
  pair.a->Start(); pair.b->Start();
  charm::SharedMemory memory;
  charm::Mapping mapping;
  if (charm::SharedMemory::create(4096, &memory) != charm::Status::Ok ||
      memory.map(charm::MemoryAccess::ReadWrite, &mapping) != charm::Status::Ok) return false;
  static_cast<uint8_t*>(mapping.data())[0] = 91;
  std::vector<mojo::PlatformHandle> handles;
  for (unsigned i = 0; i < 20; ++i) {
    charm::UniqueHandle readonly;
    if (memory.share(charm::rights::kRead | charm::rights::kMap | charm::rights::kTransfer |
                     charm::rights::kDuplicate, &readonly) != charm::Status::Ok) return false;
    handles.emplace_back(std::move(readonly));
  }
  std::array<uint8_t, 8> payload{1,2,3,4,5,6,7,8};
  if (ipcz) pair.a->WriteNextIpczMessage(payload, std::move(handles));
  else {
    auto message = Channel::Message::CreateMessage(payload.size(), handles.size());
    memcpy(message->mutable_payload(), payload.data(), payload.size());
    message->SetHandles(std::move(handles));
    pair.a->Write(std::move(message));
  }
  if (!Wait(pair.first, pair.second, [&] { return pair.second.messages.size() == 1; })) return false;
  auto& received = pair.second.messages[0];
  if (received.bytes.size() != payload.size() ||
      memcmp(received.bytes.data(), payload.data(), payload.size()) || received.handles.size() != 20)
    return false;
  for (auto& handle : received.handles) {
    if (!handle.is_capability()) return false;
    auto rights = charm::handle_rights(handle.GetCapability().get());
    if (!rights.ok() || (rights.value & charm::rights::kWrite)) return false;
    charm::SharedMemory imported;
    charm::Mapping view, forbidden;
    if (charm::SharedMemory::adopt(handle.TakeCapability(), &imported) != charm::Status::Ok ||
        imported.map(charm::MemoryAccess::ReadOnly, &view) != charm::Status::Ok ||
        static_cast<const uint8_t*>(view.data())[0] != 91 ||
        imported.map(charm::MemoryAccess::ReadWrite, &forbidden) != charm::Status::AccessDenied)
      return false;
  }
  // Exercise the reverse direction and a real peer-close callback.
  if (ipcz) pair.b->WriteNextIpczMessage(payload, {});
  else {
    auto reply = Channel::Message::CreateMessage(payload.size(), 0);
    memcpy(reply->mutable_payload(), payload.data(), payload.size());
    pair.b->Write(std::move(reply));
  }
  if (!Wait(pair.first, pair.second, [&] { return pair.first.messages.size() == 1; })) return false;
  pair.b->ShutDown();
  return Wait(pair.first, pair.second, [&] { return pair.first.errors == 1; }) &&
         pair.first.error == Channel::Error::kDisconnected && !pair.second.errors;
}
bool Backpressure() {
  Pair pair;
  pair.a->Start();
  constexpr size_t size = 5 * 1024 * 1024 + 17;
  auto message = Channel::Message::CreateMessage(size, 0);
  auto* bytes = static_cast<uint8_t*>(message->mutable_payload());
  for (size_t i = 0; i < size; ++i) bytes[i] = static_cast<uint8_t>(i * 37 + i / 65504);
  pair.a->Write(std::move(message));
  base::RunLoop().RunUntilIdle(); // The reader has deliberately not started.
  abi::syscall::ChannelState state{};
  if (charm::channel_query(pair.raw_remote, &state) != charm::Status::Ok ||
      !state.messages || state.queued_bytes >= size || pair.first.errors) return false;
  pair.b->Start();
  if (!Wait(pair.first, pair.second, [&] { return pair.second.messages.size() == 1; })) return false;
  const auto& data = pair.second.messages[0].bytes;
  if (data.size() != size) return false;
  for (size_t i = 0; i < size; ++i)
    if (data[i] != static_cast<uint8_t>(i * 37 + i / 65504)) return false;
  return !pair.first.errors && !pair.second.errors;
}
bool ConcurrentWrites() {
  Pair pair;
  pair.a->Start(); pair.b->Start();
  auto writer = [&](uint32_t id) {
    for (uint32_t index = 0; index < 24; ++index) {
      auto message = Channel::Message::CreateMessage(8, 0);
      uint32_t values[2]{id, index};
      memcpy(message->mutable_payload(), values, sizeof(values));
      pair.a->Write(std::move(message));
    }
  };
  std::thread first(writer, 0), second(writer, 1);
  bool received = Wait(pair.first, pair.second, [&] { return pair.second.messages.size() == 48; });
  first.join(); second.join();
  if (!received) return false;
  uint32_t next[2]{};
  for (const auto& message : pair.second.messages) {
    uint32_t values[2];
    if (message.bytes.size() != sizeof(values)) return false;
    memcpy(values, message.bytes.data(), sizeof(values));
    if (values[0] > 1 || values[1] != next[values[0]]++) return false;
  }
  return next[0] == 24 && next[1] == 24;
}
bool QueueLimitAndShutdown() {
  {
    Pair pair;
    for (int i = 0; i < 129; ++i) pair.a->Write(Channel::Message::CreateMessage(1, 0));
    if (pair.first.errors) return false; // Never re-enter a caller from Write.
    pair.a->Start();
    if (!Wait(pair.first, pair.second, [&] { return pair.first.errors == 1; }) ||
        !pair.second.messages.empty()) return false;
  }
  {
    Pair pair;
    pair.a->Start(); pair.b->Start();
    pair.a->Write(Channel::Message::CreateMessage(65536, 0));
    pair.b->ShutDown();
    base::RunLoop().RunUntilIdle();
    if (!pair.second.messages.empty() || pair.second.errors) return false;
  }
  return true;
}
// Deliberately constructed wire input, independent of the adapter's encoder.
struct WireFrame {
  uint32_t magic = 0x314f4d43;
  uint16_t version = 1, flags = 3;
  uint64_t id = 1;
  uint32_t total = 16, offset = 0;
  uint16_t handles = 0, handle_offset = 0, fragment_handles = 0, reserved = 0;
};
static_assert(sizeof(WireFrame) == 32);
bool MalformedMessages() {
  for (unsigned test = 0; test < 10; ++test) {
    Pair pair;
    pair.a->Start();
    WireFrame frame;
    Channel::Message::Header inner{};
    inner.num_bytes = sizeof(inner);
    inner.num_header_bytes = sizeof(inner);
    inner.message_type = Channel::Message::MessageType::NORMAL;
    switch (test) {
      case 0: frame.magic ^= 1; break;
      case 1: frame.version = 2; break;
      case 2: frame.flags |= 4; break;
      case 3: frame.id = 2; break;
      case 4: frame.offset = 1; break;
      case 5: frame.total = 8 * 1024 * 1024 + 1; break;
      case 6: frame.fragment_handles = 1; break;
      case 7: frame.reserved = 1; break;
      case 8: frame.flags = 1; break; // Complete data without LAST.
      case 9: inner.num_bytes += 1; break;
    }
    std::array<uint8_t, sizeof(frame) + sizeof(inner)> bytes;
    memcpy(bytes.data(), &frame, sizeof(frame));
    memcpy(bytes.data() + sizeof(frame), &inner, sizeof(inner));
    if (charm::channel_write(pair.raw_remote, bytes.data(), bytes.size(), nullptr, 0) != charm::Status::Ok ||
        !Wait(pair.first, pair.second, [&] { return pair.first.errors == 1; }) ||
        pair.first.error != Channel::Error::kReceivedMalformedData || !pair.first.messages.empty())
      return false;
  }
  return true;
}
}
bool CheckNativeMojoChannels() {
  base::SingleThreadTaskExecutor executor(base::MessagePumpType::IO);
  int fds = charm_open_fd_count();
  if (!Synchronization()) { printf("Native Mojo: Abseil synchronization failed\n"); return false; }
  if (!HandleOwnership()) { printf("Native Mojo: handle ownership failed\n"); return false; }
  if (!Transfer(false)) { printf("Native Mojo: normal channel transfer failed\n"); return false; }
  if (!Transfer(true)) { printf("Native Mojo: ipcz channel transfer failed\n"); return false; }
  if (!Backpressure()) { printf("Native Mojo: backpressure failed\n"); return false; }
  if (!ConcurrentWrites()) { printf("Native Mojo: concurrent writes failed\n"); return false; }
  if (!QueueLimitAndShutdown()) { printf("Native Mojo: queue limit/shutdown failed\n"); return false; }
  if (!MalformedMessages()) { printf("Native Mojo: malformed-input rejection failed\n"); return false; }
  if (charm_open_fd_count() != fds) { printf("Native Mojo: readiness descriptor leak\n"); return false; }
  printf("Native Mojo: normal/ipcz channels, large messages, capability batches, backpressure and rejection ok\n");
  return true;
}
