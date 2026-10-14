#include "spinlock.hpp"

#include <arch/x86_64/cpu.hpp>

#include "../core/panic.hpp"
#include "../smp/cpu.hpp"

namespace kernel::sync {

namespace {

bool save_and_disable_interrupts() {
  bool was = (arch::x86_64::read_rflags() & arch::x86_64::kRflagsIf) != 0;
  arch::x86_64::cli();
  return was;
}

void restore_interrupts(bool was) {
  if (was) arch::x86_64::sti();
}

void check_order(const Spinlock& lock) {
  if (!smp::percpu_ready()) return;
  smp::Cpu& c = smp::this_cpu();
  for (uint32_t i = 0; i < c.held_count; ++i) {
    const Spinlock* held = c.held[i];
    if (held == &lock) panic("lock: recursive acquire of {}", lock.name());
    if (held->rank() >= lock.rank()) {
      panic("lock order violation: taking {} (rank {}) while holding {} (rank {})", lock.name(),
            static_cast<uint32_t>(lock.rank()), held->name(), static_cast<uint32_t>(held->rank()));
    }
  }
  if (c.held_count == smp::kMaxHeldLocks) panic("lock: too many locks held");
}

void note_acquired(const Spinlock& lock) {
  if (!smp::percpu_ready()) return;
  smp::Cpu& c = smp::this_cpu();
  c.held[c.held_count++] = &lock;
}

void note_released(const Spinlock& lock) {
  if (!smp::percpu_ready()) return;
  smp::Cpu& c = smp::this_cpu();
  // Обычно отпускается последняя взятая, но порядок отпускания не обязателен.
  for (uint32_t i = c.held_count; i > 0; --i) {
    if (c.held[i - 1] == &lock) {
      for (uint32_t k = i - 1; k + 1 < c.held_count; ++k) c.held[k] = c.held[k + 1];
      --c.held_count;
      return;
    }
  }
  panic("lock: releasing {} which is not held", lock.name());
}

}  // namespace

bool Spinlock::lock() {
  bool irq = save_and_disable_interrupts();
  check_order(*this);
  uint32_t ticket = __atomic_fetch_add(&next_, 1, __ATOMIC_RELAXED);
  while (__atomic_load_n(&serving_, __ATOMIC_ACQUIRE) != ticket) arch::x86_64::pause();
  note_acquired(*this);
  return irq;
}

bool Spinlock::try_lock(bool* interrupts_were_enabled) {
  bool irq = save_and_disable_interrupts();
  uint32_t serving = __atomic_load_n(&serving_, __ATOMIC_ACQUIRE);
  uint32_t expected = serving;
  // Свободна, только если очередь пуста: next == serving.
  if (!__atomic_compare_exchange_n(&next_, &expected, serving + 1, false, __ATOMIC_ACQUIRE,
                                   __ATOMIC_RELAXED)) {
    restore_interrupts(irq);
    return false;
  }
  note_acquired(*this);
  *interrupts_were_enabled = irq;
  return true;
}

void Spinlock::unlock(bool interrupts_were_enabled) {
  note_released(*this);
  __atomic_store_n(&serving_, serving_ + 1, __ATOMIC_RELEASE);
  restore_interrupts(interrupts_were_enabled);
}

}  // namespace kernel::sync
