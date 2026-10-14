#pragma once
// Обёртки системных вызовов (docs/abi/syscalls.md). Соглашение: номер в RAX,
// аргументы в RDI, RSI, RDX, R10, R8, R9; статус в RAX, значение в RDX;
// RCX и R11 портит сама инструкция SYSCALL, остальное ядро сохраняет.

#include <stddef.h>
#include <stdint.h>

#include <abi/syscall.hpp>

namespace charm {

using abi::syscall::Number;
using abi::syscall::Status;

struct SysResult {
  Status status;
  uint64_t value;
  bool ok() const { return status == Status::Ok; }
};

inline SysResult syscall0(Number n) {
  uint64_t rax = static_cast<uint64_t>(n), rdx;
  asm volatile("syscall" : "+a"(rax), "=d"(rdx) : : "rcx", "r11", "memory");
  return {static_cast<Status>(rax), rdx};
}

inline SysResult syscall1(Number n, uint64_t a1) {
  uint64_t rax = static_cast<uint64_t>(n), rdx;
  asm volatile("syscall" : "+a"(rax), "=d"(rdx) : "D"(a1) : "rcx", "r11", "memory");
  return {static_cast<Status>(rax), rdx};
}

inline SysResult syscall2(Number n, uint64_t a1, uint64_t a2) {
  uint64_t rax = static_cast<uint64_t>(n), rdx;
  asm volatile("syscall" : "+a"(rax), "=d"(rdx) : "D"(a1), "S"(a2) : "rcx", "r11", "memory");
  return {static_cast<Status>(rax), rdx};
}

inline SysResult syscall3(Number n, uint64_t a1, uint64_t a2, uint64_t a3) {
  uint64_t rax = static_cast<uint64_t>(n), rdx = a3;
  asm volatile("syscall" : "+a"(rax), "+d"(rdx) : "D"(a1), "S"(a2) : "rcx", "r11", "memory");
  return {static_cast<Status>(rax), rdx};
}

inline SysResult syscall5(Number n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
                          uint64_t a5) {
  uint64_t rax = static_cast<uint64_t>(n), rdx = a3;
  register uint64_t r10 asm("r10") = a4;
  register uint64_t r8 asm("r8") = a5;
  asm volatile("syscall"
               : "+a"(rax), "+d"(rdx)
               : "D"(a1), "S"(a2), "r"(r10), "r"(r8)
               : "rcx", "r11", "memory");
  return {static_cast<Status>(rax), rdx};
}

inline SysResult syscall6(Number n, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
                          uint64_t a5, uint64_t a6) {
  uint64_t rax = static_cast<uint64_t>(n), rdx = a3;
  register uint64_t r10 asm("r10") = a4;
  register uint64_t r8 asm("r8") = a5;
  register uint64_t r9 asm("r9") = a6;
  asm volatile("syscall"
               : "+a"(rax), "+d"(rdx)
               : "D"(a1), "S"(a2), "r"(r10), "r"(r8), "r"(r9)
               : "rcx", "r11", "memory");
  return {static_cast<Status>(rax), rdx};
}

// Номер, которого нет в ABI (для проверки NotSupported).
inline SysResult raw_syscall(uint64_t number) { return syscall0(static_cast<Number>(number)); }

inline SysResult abi_version() { return syscall0(Number::AbiVersion); }

inline SysResult debug_write(const void* buf, uint64_t len) {
  return syscall2(Number::DebugWrite, reinterpret_cast<uint64_t>(buf), len);
}

inline size_t length(const char* s) {
  size_t n = 0;
  while (s[n] != '\0') ++n;
  return n;
}

inline void print(const char* s) { debug_write(s, length(s)); }

[[noreturn]] inline void exit(int64_t code) {
  syscall1(Number::ProcessExit, static_cast<uint64_t>(code));
  __builtin_unreachable();
}

inline void yield() { syscall0(Number::ThreadYield); }
inline uint64_t now_ns() { return syscall0(Number::ClockMonotonic).value; }
inline void sleep_until(uint64_t deadline_ns) { syscall1(Number::Nanosleep, deadline_ns); }

// --- v1.1: хендлы, каналы, VMO (docs/abi/ipc.md) ---------------------------------

namespace rights = abi::syscall::rights;
using Handle = uint32_t;

inline Status handle_close(Handle h) { return syscall1(Number::HandleClose, h).status; }
inline SysResult handle_duplicate(Handle h, uint32_t r) {
  return syscall2(Number::HandleDuplicate, h, r);
}
inline SysResult handle_revoke(Handle h) { return syscall1(Number::HandleRevoke, h); }

inline Status channel_create(Handle pair[2]) {
  return syscall1(Number::ChannelCreate, reinterpret_cast<uint64_t>(pair)).status;
}
inline Status channel_write(Handle h, const void* bytes, uint64_t len, const Handle* handles,
                            uint64_t count) {
  return syscall5(Number::ChannelWrite, h, reinterpret_cast<uint64_t>(bytes), len,
                  reinterpret_cast<uint64_t>(handles), count)
      .status;
}
// value: (байт << 32) | хендлов.
inline SysResult channel_read(Handle h, void* bytes, uint64_t cap, Handle* handles,
                              uint64_t hcap, uint64_t deadline) {
  return syscall6(Number::ChannelRead, h, reinterpret_cast<uint64_t>(bytes), cap,
                  reinterpret_cast<uint64_t>(handles), hcap, deadline);
}
inline constexpr uint64_t kForever = abi::syscall::kInfiniteDeadline;

inline SysResult vmo_create(uint64_t size) { return syscall1(Number::VmoCreate, size); }
inline SysResult vmo_map(Handle h, uint32_t r) { return syscall2(Number::VmoMap, h, r); }
inline Status vmo_unmap(uint64_t addr) { return syscall1(Number::VmoUnmap, addr).status; }

// --- v1.2: порты (docs/abi/driver.md §4) -----------------------------------------
using abi::syscall::PortPacket;
inline SysResult port_create() { return syscall0(Number::PortCreate); }
inline Status port_wait(Handle port, PortPacket* out, uint64_t deadline) {
  return syscall3(Number::PortWait, port, reinterpret_cast<uint64_t>(out), deadline).status;
}
inline Status object_bind(Handle port, Handle h, uint64_t key) {
  return syscall3(Number::ObjectBind, port, h, key).status;
}

// DMA-буфер устройства (docs/abi/driver.md §5): VMO и адрес для устройства.
inline SysResult dma_alloc(Handle dev, uint64_t size, uint64_t flags, uint64_t* device_addr) {
  return syscall5(Number::DmaAlloc, dev, size, flags, reinterpret_cast<uint64_t>(device_addr), 0);
}

}  // namespace charm
