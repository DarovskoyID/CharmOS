#pragma once
// Данные, свои у каждого процессора. Доступ — через GS: база GS указывает на
// структуру Cpu, первое поле — указатель на саму структуру (gs:0).
// База ставится через IA32_KERNEL_GS_BASE + SWAPGS (SDM Vol. 2B, SWAPGS):
// так же будет устроен вход из ring 3 в M3.

#include <arch/x86_64/gdt.hpp>
#include <stdint.h>

namespace kernel::sync {
class Spinlock;
}

namespace kernel::smp {

inline constexpr uint32_t kMaxCpus = 64;
inline constexpr uint32_t kMaxHeldLocks = 8;

struct Cpu {
  Cpu* self;  // gs:0 — обязательно первое поле
  // Используются syscall_entry.S по фиксированным смещениям (см. static_assert).
  uint64_t kernel_rsp;  // gs:8 — вершина стека ядра текущего потока
  uint64_t user_rsp;    // gs:16 — RSP пользователя на время входа в syscall
  uint32_t index;
  uint32_t apic_id;
  volatile bool online;

  arch::x86_64::CpuDescriptors descriptors;
  uint64_t ist_tops[arch::x86_64::kIstCount];

  // Проверка порядка захвата блокировок (kernel/sync/spinlock.hpp).
  const sync::Spinlock* held[kMaxHeldLocks];
  uint32_t held_count;
};

static_assert(__builtin_offsetof(Cpu, kernel_rsp) == 8);
static_assert(__builtin_offsetof(Cpu, user_rsp) == 16);

// Структура текущего процессора. До smp::early_init — недоступна.
inline Cpu& this_cpu() {
  Cpu* c;
  asm volatile("mov %%gs:0, %0" : "=r"(c));
  return *c;
}

// true, когда GS уже указывает на Cpu (блокировки до этого не проверяют порядок).
bool percpu_ready();

uint32_t cpu_count();          // сколько CPU запущено (включая BSP)
Cpu& cpu(uint32_t index);

}  // namespace kernel::smp
