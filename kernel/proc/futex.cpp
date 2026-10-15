// Futex (docs/abi/process.md §4): ожидание по адресу в памяти процесса.
// Один список на всё ядро (правило 9: хеш-таблица — после замеров).

#include <abi/syscall.hpp>

#include "../platform/platform.hpp"
#include "../sched/sched.hpp"
#include "../sync/spinlock.hpp"
#include "process.hpp"
#include "uaccess.hpp"

namespace kernel::proc {

namespace {

struct Waiter {
  sched::Thread* thread;
  Process* process;
  uint64_t addr;
  Waiter* next;
  bool woken;
};

// Под g_lock значение читается и ожидающий встаёт в список атомарно
// относительно futex_wake: пробуждение не теряется.
constinit sync::Spinlock g_lock{sync::LockRank::Wait, "futex"};
constinit Waiter* g_waiters = nullptr;

void unlink(Waiter* w) {
  for (Waiter** it = &g_waiters; *it != nullptr; it = &(*it)->next) {
    if (*it == w) {
      *it = w->next;
      return;
    }
  }
}

bool valid(uint64_t addr) { return addr % 4 == 0 && addr >= kUserImageMin && addr <= abi::syscall::kUserTop - 4; }

}  // namespace

Status sys_futex_wait(uint64_t addr, uint64_t expected, uint64_t deadline) {
  if (addr % 4 != 0) return Status::InvalidArgs;
  if (!valid(addr)) return Status::BadAddress;
  Waiter w{sched::current(), current_process(), addr, nullptr, false};
  bool irq = g_lock.lock();
  uint32_t value;
  if (!copy_from_user(&value, addr, sizeof value)) {
    g_lock.unlock(irq);
    return Status::BadAddress;
  }
  if (value != static_cast<uint32_t>(expected)) {
    g_lock.unlock(irq);
    return Status::WouldBlock;
  }
  if (sched::interrupted()) {
    g_lock.unlock(irq);
    return Status::Interrupted;
  }
  if (deadline != abi::syscall::kInfiniteDeadline && platform::now_ns() >= deadline) {
    g_lock.unlock(irq);
    return Status::TimedOut;
  }
  w.next = g_waiters;
  g_waiters = &w;
  sched::block_interruptible_until(deadline);
  g_lock.unlock(false);
  sched::schedule();
  sched::end_interruptible();
  g_lock.lock();
  bool woken = w.woken;
  if (!woken) unlink(&w);  // срок или прерывание — ещё в списке
  g_lock.unlock(irq);
  if (woken) return Status::Ok;
  return sched::interrupted() ? Status::Interrupted : Status::TimedOut;
}

Status sys_futex_wake(uint64_t addr, uint64_t count, uint64_t* woken) {
  *woken = 0;
  if (addr % 4 != 0) return Status::InvalidArgs;
  if (!valid(addr)) return Status::BadAddress;
  Process* p = current_process();
  sync::SpinGuard guard(g_lock);
  for (Waiter** it = &g_waiters; *it != nullptr && *woken < count;) {
    Waiter* w = *it;
    if (w->process != p || w->addr != addr) {
      it = &w->next;
      continue;
    }
    *it = w->next;
    w->woken = true;  // после wake структура (на стеке ожидающего) не трогается
    sched::wake(w->thread);
    ++*woken;
  }
  return Status::Ok;
}

}  // namespace kernel::proc
