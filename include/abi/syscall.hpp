#pragma once
// Syscall ABI v1.0. Нормативный документ — docs/abi/syscalls.md.
// Номера вызовов и коды статусов никогда не переиспользуются и не меняют смысл.

#include <stdint.h>

namespace abi::syscall {

inline constexpr uint16_t kVersionMajor = 1;
inline constexpr uint16_t kVersionMinor = 0;

// §5
enum class Number : uint64_t {
  AbiVersion = 0,
  DebugWrite = 1,
  ProcessExit = 2,
  ThreadYield = 3,
  ClockMonotonic = 4,
  Nanosleep = 5,
};

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
