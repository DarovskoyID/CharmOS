// Native message-preserving Mojo channel. Wire contract: chromium-mojo.md.
#include "mojo/core/channel.h"
#include "mojo/core/ipcz_driver/envelope.h"
#include "base/check.h"
#include "base/files/scoped_file.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/message_loop/message_pump_for_io.h"
#include "base/task/current_thread.h"
#include <charm/libc.h>
#include <charm/handle.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <string.h>

namespace mojo::core {
namespace {
constexpr uint32_t kMagic = 0x314f4d43;
constexpr uint16_t kFirst = 1, kLast = 2;
constexpr size_t kMaxBytes = 8 * 1024 * 1024;
constexpr size_t kMaxHandles = 64;
constexpr size_t kQueueBytes = 16 * 1024 * 1024;
constexpr size_t kQueueMessages = 128;
struct Frame {
  uint32_t magic;
  uint16_t version, flags;
  uint64_t id;
  uint32_t total_bytes, offset;
  uint16_t total_handles, handle_offset, fragment_handles, reserved;
};
static_assert(sizeof(Frame) == 32 && __builtin_offsetof(Frame, id) == 8);
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);
constexpr size_t kFragmentBytes = abi::syscall::kChannelMaxBytes - sizeof(Frame);

class ChannelCharmOS final : public Channel,
                            public base::MessagePumpForIO::FdWatcher,
                            public base::CurrentThread::DestructionObserver {
 public:
  ChannelCharmOS(Delegate* delegate, ConnectionParams params, HandlePolicy policy,
                scoped_refptr<base::SingleThreadTaskRunner> runner)
      : Channel(delegate, policy, DispatchBufferPolicy::kUnmanaged),
        runner_(std::move(runner)),
        endpoint_(params.TakeEndpoint().TakePlatformHandle().TakeCapability()),
        leak_(params.leak_endpoint()) {}

  void Start() override {
    runner_->PostTask(FROM_HERE, base::BindOnce(&ChannelCharmOS::StartIO,
                                             base::RetainedRef(this)));
  }
  void ShutDownImpl() override {
    stop_requested_.store(true, std::memory_order_release);
    {
      base::AutoLock lock(lock_);
      reject_writes_ = true;
    }
    runner_->PostTask(FROM_HERE, base::BindOnce(&ChannelCharmOS::CloseIO,
                                             base::RetainedRef(this)));
  }
  void LeakHandle() override {
    CHECK(runner_->RunsTasksInCurrentSequence());
    leak_ = true;
  }
  void Write(MessagePtr message) override {
    Pending pending;
    pending.message = std::move(message);
    bool valid = pending.message && pending.message->data_num_bytes() <= kMaxBytes;
    if (valid) {
      pending.handles = pending.message->TakeHandles();
      valid = pending.handles.size() <= kMaxHandles;
      for (const auto& handle : pending.handles) {
        if (handle.owning_process().IsValid() || !handle.handle().is_capability() ||
            !handle.handle().GetCapability()) valid = false;
      }
    }
    {
      base::AutoLock lock(lock_);
      if (reject_writes_) return;
      if (!valid || outgoing_.size() >= kQueueMessages ||
          pending.message->data_num_bytes() > kQueueBytes - queued_bytes_) {
        reject_writes_ = true;
        write_error_ = true;
      } else {
        queued_bytes_ += pending.message->data_num_bytes();
        outgoing_.push_back(std::move(pending));
      }
    }
    runner_->PostTask(FROM_HERE, base::BindOnce(&ChannelCharmOS::Drain,
                                             base::RetainedRef(this)));
  }

 private:
  struct Pending {
    MessagePtr message;
    std::vector<PlatformHandleInTransit> handles;
    size_t offset = 0, handle_offset = 0;
  };
  ~ChannelCharmOS() override {
    CHECK(!observing_);
    CHECK(!readiness_.is_valid());
    if (leak_) (void)endpoint_.release();
  }
  void StartIO() {
    CHECK(runner_->RunsTasksInCurrentSequence());
    if (closed_ || stop_requested_.load(std::memory_order_acquire)) {
      CloseIO(); return;
    }
    CHECK(!started_);
    started_ = true;
    keep_alive_ = this;
    base::CurrentThread::Get()->AddDestructionObserver(this);
    observing_ = true;
    abi::syscall::ChannelState state{};
    if (!endpoint_ || charm::channel_query(endpoint_.get(), &state) != charm::Status::Ok) {
      Fail(Error::kConnectionFailed); return;
    }
    readiness_.reset(charm_channel_watch_fd(endpoint_.get(), 3));
    if (!readiness_.is_valid() ||
        !base::CurrentIOThread::Get()->WatchFileDescriptor(
            readiness_.get(), true, base::MessagePumpForIO::WATCH_READ,
            &read_watch_, this)) {
      Fail(Error::kConnectionFailed); return;
    }
    Drain();
  }
  void CloseIO() {
    CHECK(runner_->RunsTasksInCurrentSequence());
    scoped_refptr<ChannelCharmOS> alive(this);
    if (closed_) return;
    closed_ = true;
    stop_requested_.store(true, std::memory_order_release);
    read_watch_.StopWatchingFileDescriptor();
    write_watch_.StopWatchingFileDescriptor();
    waiting_write_ = false;
    readiness_.reset();
    if (leak_) (void)endpoint_.release();
    else endpoint_.reset();
    incoming_.clear();
    incoming_handles_.clear();
    {
      base::AutoLock lock(lock_);
      reject_writes_ = true;
      outgoing_.clear();
      queued_bytes_ = 0;
    }
    if (observing_) {
      base::CurrentThread::Get()->RemoveDestructionObserver(this);
      observing_ = false;
    }
    keep_alive_ = nullptr;
  }
  void WillDestroyCurrentMessageLoop() override { CloseIO(); }
  void Fail(Error error) {
    scoped_refptr<ChannelCharmOS> alive(this);
    bool notify = started_ && !closed_ && !stop_requested_.load(std::memory_order_acquire);
    CloseIO();
    if (notify) OnError(error);
  }
  void ArmWrite() {
    if (waiting_write_ || closed_) return;
    waiting_write_ = true;
    if (!base::CurrentIOThread::Get()->WatchFileDescriptor(
            readiness_.get(), false, base::MessagePumpForIO::WATCH_WRITE,
            &write_watch_, this)) Fail(Error::kDisconnected);
  }
  void Drain() {
    CHECK(runner_->RunsTasksInCurrentSequence());
    if (!started_ || closed_ || stop_requested_.load(std::memory_order_acquire))
      return;
    bool failed;
    { base::AutoLock lock(lock_); failed = write_error_; }
    if (failed) { Fail(Error::kDisconnected); return; }
    if (waiting_write_) return;
    for (unsigned batch = 0; batch < 32; ++batch) {
      charm::Status status = charm::Status::Ok;
      bool write_error = false;
      {
        base::AutoLock lock(lock_);
        if (write_error_) write_error = true;
        else if (outgoing_.empty()) return;
        else {
          auto& pending = outgoing_.front();
          size_t size = pending.message->data_num_bytes();
          size_t bytes = std::min(size - pending.offset, kFragmentBytes);
          size_t handles = std::min(pending.handles.size() - pending.handle_offset, size_t{16});
          bool last = pending.offset + bytes == size &&
                      pending.handle_offset + handles == pending.handles.size();
          Frame frame{kMagic, 1,
                      static_cast<uint16_t>((!pending.offset && !pending.handle_offset ? kFirst : 0) |
                                            (last ? kLast : 0)),
                      outgoing_id_, static_cast<uint32_t>(size),
                      static_cast<uint32_t>(pending.offset),
                      static_cast<uint16_t>(pending.handles.size()),
                      static_cast<uint16_t>(pending.handle_offset),
                      static_cast<uint16_t>(handles), 0};
          auto& packet = write_packet_;
          memcpy(packet.data(), &frame, sizeof(frame));
          if (bytes) memcpy(packet.data() + sizeof(frame),
                            static_cast<const uint8_t*>(pending.message->data()) + pending.offset, bytes);
          std::array<charm::Handle, 16> capabilities{};
          for (size_t i = 0; i < handles; ++i)
            capabilities[i] = pending.handles[pending.handle_offset + i].handle().GetCapability().get();
          status = charm::channel_write(endpoint_.get(), packet.data(), sizeof(frame) + bytes,
                                        capabilities.data(), handles);
          if (status == charm::Status::Ok) {
            // The kernel consumed exactly these handles; never resend or close them.
            for (size_t i = 0; i < handles; ++i)
              pending.handles[pending.handle_offset + i].CompleteTransit();
            pending.offset += bytes;
            pending.handle_offset += handles;
            if (last) {
              queued_bytes_ -= size;
              outgoing_.pop_front();
              if (++outgoing_id_ == 0) write_error = true;
            }
          }
        }
      }
      if (write_error) { Fail(Error::kDisconnected); return; }
      if (status == charm::Status::WouldBlock) { ArmWrite(); return; }
      if (status != charm::Status::Ok) { Fail(Error::kDisconnected); return; }
    }
    runner_->PostTask(FROM_HERE, base::BindOnce(&ChannelCharmOS::Drain,
                                             base::RetainedRef(this)));
  }
  void OnFileCanWriteWithoutBlocking(int fd) override {
    scoped_refptr<ChannelCharmOS> alive(this);
    if (fd != readiness_.get() || closed_) return;
    write_watch_.StopWatchingFileDescriptor();
    waiting_write_ = false;
    Drain();
  }
  void OnFileCanReadWithoutBlocking(int fd) override {
    scoped_refptr<ChannelCharmOS> alive(this);
    if (fd != readiness_.get() || closed_ || stop_requested_.load(std::memory_order_acquire)) return;
    for (unsigned batch = 0; batch < 32; ++batch) {
      auto& packet = read_packet_;
      std::array<charm::Handle, 16> attachments{};
      auto result = charm::channel_read(endpoint_.get(), packet.data(), packet.size(),
                                        attachments.data(), attachments.size(), 0);
      if (result.status == charm::Status::WouldBlock || result.status == charm::Status::TimedOut)
        return;
      if (result.status == charm::Status::Interrupted) continue;
      if (!result.ok()) {
        Fail(incoming_.empty() ? Error::kDisconnected : Error::kReceivedMalformedData);
        return;
      }
      size_t size = result.value >> 32;
      size_t count = static_cast<uint32_t>(result.value);
      std::vector<PlatformHandle> handles;
      for (size_t i = 0; i < count; ++i)
        handles.emplace_back(charm::UniqueHandle::adopt(attachments[i]));
      if (!Consume(base::span(packet).first(size), std::move(handles))) {
        Fail(Error::kReceivedMalformedData); return;
      }
      if (closed_ || stop_requested_.load(std::memory_order_acquire)) return;
    }
  }
  bool Consume(base::span<const uint8_t> packet, std::vector<PlatformHandle> handles) {
    if (packet.size() < sizeof(Frame)) return false;
    Frame frame;
    memcpy(&frame, packet.data(), sizeof(frame));
    auto payload = packet.subspan(sizeof(Frame));
    if (frame.magic != kMagic || frame.version != 1 || frame.reserved ||
        (frame.flags & ~(kFirst | kLast)) || frame.id != incoming_id_ ||
        frame.total_bytes > kMaxBytes || frame.total_handles > kMaxHandles ||
        frame.fragment_handles != handles.size() || (payload.empty() && handles.empty())) return false;
    if (frame.total_handles && handle_policy() == HandlePolicy::kRejectHandles) return false;
    if (frame.flags & kFirst) {
      if (!incoming_.empty() || frame.offset || frame.handle_offset ||
          frame.total_bytes < (is_for_ipcz() ? Message::kMinIpczHeaderSize : sizeof(Message::Header)))
        return false;
      incoming_.resize(frame.total_bytes);
      incoming_offset_ = 0;
      expected_handles_ = frame.total_handles;
    } else if (incoming_.empty()) return false;
    if (frame.total_bytes != incoming_.size() || frame.total_handles != expected_handles_ ||
        frame.offset != incoming_offset_ || frame.handle_offset != incoming_handles_.size() ||
        payload.size() > incoming_.size() - incoming_offset_ ||
        handles.size() > expected_handles_ - incoming_handles_.size()) return false;
    if (!payload.empty()) memcpy(incoming_.data() + incoming_offset_, payload.data(), payload.size());
    incoming_offset_ += payload.size();
    for (auto& handle : handles) incoming_handles_.push_back(std::move(handle));
    bool complete = incoming_offset_ == incoming_.size() && incoming_handles_.size() == expected_handles_;
    if (complete != bool(frame.flags & kLast)) return false;
    if (!complete) return true;

    // Keep the dispatched buffer alive even if the delegate shuts the channel.
    auto data = std::move(incoming_);
    auto received = std::move(incoming_handles_);
    incoming_.clear();
    incoming_handles_.clear();
    incoming_offset_ = expected_handles_ = 0;
    if (++incoming_id_ == 0 || !ValidMessage(data, received.size())) return false;
    size_t consumed = 0;
    return TryDispatchMessage(data, std::move(received), nullptr, &consumed) == DispatchResult::kOK &&
           consumed == data.size();
  }
  bool ValidMessage(base::span<const char> data, size_t handles) {
    if (is_for_ipcz()) {
      if (data.size() < Message::kMinIpczHeaderSize) return false;
      // Older headers contain only the first 8 bytes; copy that common prefix.
      Message::IpczHeader header{};
      memcpy(&header, data.data(), Message::kMinIpczHeaderSize);
      return header.size >= Message::kMinIpczHeaderSize && header.size <= data.size() &&
             header.num_bytes == data.size() && header.num_handles == handles;
    }
    if (data.size() < sizeof(Message::Header)) return false;
    Message::Header header;
    memcpy(&header, data.data(), sizeof(header));
    return header.num_bytes == data.size() && header.num_handles == handles &&
           header.num_header_bytes >= sizeof(header) && header.num_header_bytes <= data.size() &&
           IsAlignedForChannelMessage(header.num_header_bytes) &&
           header.message_type == Message::MessageType::NORMAL;
  }
  bool GetReadPlatformHandles(const void*, size_t, size_t, const void*, size_t,
                              std::vector<PlatformHandle>*) override { return false; }
  bool GetReadPlatformHandlesForIpcz(size_t, std::vector<PlatformHandle>&) override { return false; }

  const scoped_refptr<base::SingleThreadTaskRunner> runner_;
  charm::UniqueHandle endpoint_;
  base::ScopedFD readiness_;
  base::MessagePumpForIO::FdWatchController read_watch_{FROM_HERE}, write_watch_{FROM_HERE};
  scoped_refptr<ChannelCharmOS> keep_alive_;
  base::Lock lock_;
  std::deque<Pending> outgoing_;
  size_t queued_bytes_ = 0;
  bool reject_writes_ = false, write_error_ = false;
  std::atomic<bool> stop_requested_{false};
  bool started_ = false, closed_ = false, observing_ = false, waiting_write_ = false, leak_;
  uint64_t outgoing_id_ = 1, incoming_id_ = 1;
  std::vector<char> incoming_;
  std::vector<PlatformHandle> incoming_handles_;
  size_t incoming_offset_ = 0, expected_handles_ = 0;
  // Initial native process stacks are 64 KiB. These bounded buffers live in
  // the heap-allocated channel, never as full-sized I/O callback stack frames.
  std::array<uint8_t, abi::syscall::kChannelMaxBytes> read_packet_, write_packet_;
};
}  // namespace

scoped_refptr<Channel> Channel::Create(Delegate* delegate, ConnectionParams params,
                                     HandlePolicy policy,
                                     scoped_refptr<base::SingleThreadTaskRunner> runner) {
  return new ChannelCharmOS(delegate, std::move(params), policy, std::move(runner));
}
}  // namespace mojo::core
