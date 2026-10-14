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

}  // namespace charm
