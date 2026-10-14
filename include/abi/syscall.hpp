#pragma once
// Syscall ABI v1.1. Нормативные документы — docs/abi/syscalls.md (v1.0) и
// docs/abi/ipc.md (дополнение v1.1).
// Номера вызовов и коды статусов никогда не переиспользуются и не меняют смысл.

#include <stdint.h>

namespace abi::syscall {

inline constexpr uint16_t kVersionMajor = 1;
inline constexpr uint16_t kVersionMinor = 1;

// §5
enum class Number : uint64_t {
  AbiVersion = 0,
  DebugWrite = 1,
  ProcessExit = 2,
  ThreadYield = 3,
  ClockMonotonic = 4,
  Nanosleep = 5,
  // v1.1 (docs/abi/ipc.md §6, §8)
  HandleClose = 6,
  HandleDuplicate = 7,
  ChannelCreate = 8,
  ChannelWrite = 9,
  ChannelRead = 10,
  VmoCreate = 11,
  VmoMap = 12,
  VmoUnmap = 13,
  HandleRevoke = 14,
  // v1.2 (docs/abi/driver.md §6). abi_version сообщает 1.2, когда готовы все.
  PortCreate = 15,
  PortWait = 16,
  ObjectBind = 17,
  DeviceInfo = 18,
  DeviceMapBar = 19,
  DeviceIrq = 20,
  InterruptAck = 21,
  DmaAlloc = 22,
};

// Пакет порта (docs/abi/driver.md §4).
struct PortPacket {
  uint64_t key;
  uint32_t type;
  uint32_t reserved;
  uint64_t data0;
  uint64_t data1;
};
static_assert(sizeof(PortPacket) == 32);
inline constexpr uint32_t kPacketInterrupt = 1;
inline constexpr uint32_t kPacketChannelReadable = 2;
inline constexpr uint32_t kPacketPeerClosed = 3;
inline constexpr uint32_t kPortMaxPackets = 64;

// Права хендла (docs/abi/ipc.md §3).
namespace rights {
inline constexpr uint32_t kDuplicate = 1u << 0;
inline constexpr uint32_t kTransfer = 1u << 1;
inline constexpr uint32_t kRead = 1u << 2;
inline constexpr uint32_t kWrite = 1u << 3;
inline constexpr uint32_t kMap = 1u << 4;
inline constexpr uint32_t kAll = kDuplicate | kTransfer | kRead | kWrite | kMap;
inline constexpr uint32_t kChannelDefault = kDuplicate | kTransfer | kRead | kWrite;
inline constexpr uint32_t kPortDefault = kDuplicate | kTransfer | kRead | kWrite;
inline constexpr uint32_t kVmoDefault = kAll;
}  // namespace rights

inline constexpr uint32_t kInvalidHandle = 0;
inline constexpr uint32_t kMaxHandles = 1024;          // на процесс (ipc.md §2)
inline constexpr uint64_t kChannelMaxBytes = 64 * 1024;  // ipc.md §4
inline constexpr uint32_t kChannelMaxHandles = 16;
inline constexpr uint32_t kChannelMaxQueue = 64;
inline constexpr uint64_t kVmoMaxSize = 1ull << 30;     // ipc.md §5

// §3
enum class Status : uint64_t {
  Ok = 0,
  NotSupported = 1,
  InvalidArgs = 2,
  BadAddress = 3,
  NoMemory = 4,
  BadHandle = 5,
  AccessDenied = 6,
  WrongType = 7,
  WouldBlock = 8,
  TimedOut = 9,
  PeerClosed = 10,
  OutOfRange = 11,
  BufferTooSmall = 12,
  Busy = 13,
};

// §4
inline constexpr uint64_t kUserTop = 0x0000800000000000ull;
inline constexpr uint64_t kInfiniteDeadline = ~uint64_t{0};
inline constexpr uint64_t kDebugWriteMax = 4096;

}  // namespace abi::syscall
