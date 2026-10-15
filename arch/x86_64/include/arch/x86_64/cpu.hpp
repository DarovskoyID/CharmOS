#pragma once
// Доступ к управляющим регистрам и MSR (docs/specs/intel-sdm-vol3a.pdf §2.5, §2.2.1).

#include <stdint.h>

namespace arch::x86_64 {

inline uint64_t read_cr0() { uint64_t v; asm volatile("mov %%cr0, %0" : "=r"(v)); return v; }
inline uint64_t read_cr2() { uint64_t v; asm volatile("mov %%cr2, %0" : "=r"(v)); return v; }
inline uint64_t read_cr3() { uint64_t v; asm volatile("mov %%cr3, %0" : "=r"(v)); return v; }
inline uint64_t read_cr4() { uint64_t v; asm volatile("mov %%cr4, %0" : "=r"(v)); return v; }
inline void write_cr3(uint64_t v) { asm volatile("mov %0, %%cr3" : : "r"(v) : "memory"); }

inline uint64_t read_rflags() {
  uint64_t v;
  asm volatile("pushfq; pop %0" : "=r"(v));
  return v;
}

inline uint64_t rdmsr(uint32_t msr) {
  uint32_t lo, hi;
  asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
  return (uint64_t{hi} << 32) | lo;
}

inline void wrmsr(uint32_t msr, uint64_t v) {
  asm volatile("wrmsr"
               :
               : "c"(msr), "a"(static_cast<uint32_t>(v)), "d"(static_cast<uint32_t>(v >> 32)));
}

inline uint64_t rdtsc() {
  uint32_t lo, hi;
  asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return (uint64_t{hi} << 32) | lo;
}

inline void invlpg(uint64_t virt) { asm volatile("invlpg (%0)" : : "r"(virt) : "memory"); }
inline void cli() { asm volatile("cli" ::: "memory"); }
inline void sti() { asm volatile("sti" ::: "memory"); }
inline void pause() { asm volatile("pause"); }
// Выталкивает строку кэша с addr в память (SDM Vol. 2A, CLFLUSH). Порядок
// относительно других записей — только через mfence (там же).
inline void clflush(const volatile void* addr) {
  asm volatile("clflush (%0)" : : "r"(addr) : "memory");
}
inline void mfence() { asm volatile("mfence" ::: "memory"); }

inline uint64_t read_rbp() {
  uint64_t v;
  asm volatile("mov %%rbp, %0" : "=r"(v));
  return v;
}

inline constexpr uint64_t kRflagsIf = 1ull << 9;
inline constexpr uint64_t kRflagsDf = 1ull << 10;
inline constexpr uint64_t kCr0Wp = 1ull << 16;
inline constexpr uint32_t kMsrEfer = 0xc0000080;
// IA32_FS_BASE (SDM Vol. 4, табл. 2-2): база сегмента FS — указатель TLS потока.
inline constexpr uint32_t kMsrFsBase = 0xc0000100;
inline void set_fs_base(uint64_t base) { wrmsr(kMsrFsBase, base); }
inline constexpr uint64_t kEferNxe = 1ull << 11;

}  // namespace arch::x86_64
