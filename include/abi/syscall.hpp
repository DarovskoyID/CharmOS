#pragma once
// Syscall ABI v1.2. Нормативные документы — docs/abi/syscalls.md (v1.0),
// docs/abi/ipc.md (дополнение v1.1) и docs/abi/driver.md (дополнение v1.2).
// Номера вызовов и коды статусов никогда не переиспользуются и не меняют смысл.

#include <stdint.h>

namespace abi::syscall {

inline constexpr uint16_t kVersionMajor = 1;
inline constexpr uint16_t kVersionMinor = 2;

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
  // v1.2 (docs/abi/driver.md §6)
  PortCreate = 15,
  PortWait = 16,
  ObjectBind = 17,
  DeviceInfo = 18,
  DeviceMapBar = 19,
  DeviceIrq = 20,
  InterruptAck = 21,
  DmaAlloc = 22,
  // v1.3 (docs/abi/fs.md §4, docs/abi/driver.md §9)
  FsOpen = 23,
  FileRead = 24,
  FileWrite = 25,
  FileSync = 26,
  FsStat = 27,
  DirRead = 28,
  FsUnlink = 29,
  FileResize = 30,
  DeviceVendorCap = 31,
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

// Описание устройства для драйвера (docs/abi/driver.md §6.1).
struct DeviceInfo {
  uint16_t segment;
  uint8_t bus;
  uint8_t device;
  uint8_t function;
  uint8_t revision;
  uint16_t vendor;
  uint16_t device_id;
  uint16_t reserved0;
  uint32_t class_code;   // класс << 16 | подкласс << 8 | prog-if
  uint32_t irq_count;    // сколько индексов принимает device_irq
  uint32_t flags;        // kDeviceDma: dma_alloc доступен
  uint64_t bar_size[6];  // 0 — BAR отсутствует или это порты ввода-вывода
  uint8_t bar_kind[6];   // kBarAbsent / kBarMem32 / kBarMem64 / kBarIo
  uint8_t reserved1[2];
};
static_assert(sizeof(DeviceInfo) == 80);
static_assert(__builtin_offsetof(DeviceInfo, class_code) == 12);
static_assert(__builtin_offsetof(DeviceInfo, bar_size) == 24);
static_assert(__builtin_offsetof(DeviceInfo, bar_kind) == 72);
inline constexpr uint8_t kBarAbsent = 0;
inline constexpr uint8_t kBarMem32 = 1;
inline constexpr uint8_t kBarMem64 = 2;
inline constexpr uint8_t kBarIo = 3;
inline constexpr uint32_t kDeviceDma = 1u << 0;
inline constexpr uint64_t kDmaContiguous = 1u << 0;  // флаг dma_alloc

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
inline constexpr uint32_t kDeviceDefault = kRead | kWrite | kMap;  // без Duplicate/Transfer
inline constexpr uint32_t kInterruptDefault = kRead | kWrite;
inline constexpr uint32_t kMmioDefault = kRead | kWrite | kMap;
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
  // v1.3 (docs/abi/fs.md §6)
  IoError = 14,
  NotFound = 15,
  AlreadyExists = 16,
  NoSpace = 17,
};

// §4
inline constexpr uint64_t kUserTop = 0x0000800000000000ull;
inline constexpr uint64_t kInfiniteDeadline = ~uint64_t{0};
inline constexpr uint64_t kDebugWriteMax = 4096;

}  // namespace abi::syscall
