#include "sched.hpp"

#include <arch/x86_64/cpu.hpp>

#include "../core/panic.hpp"
#include "../mm/heap.hpp"
#include "../platform/platform.hpp"
#include "../smp/cpu.hpp"
#include "../sync/spinlock.hpp"

extern "C" void switch_context(uint64_t* save_rsp, uint64_t new_rsp);
extern "C" void thread_start();
extern "C" [[noreturn]] void thread_main();

namespace kernel::sched {

using lib::core::Result;

namespace {

struct RunQueue {
  sync::Spinlock lock{sync::LockRank::SchedQueue, "sched.rq"};
  Thread* head = nullptr;
  Thread* tail = nullptr;
  uint32_t ready = 0;
  Thread* sleepers = nullptr;  // по возрастанию wake_ns
  Thread* current = nullptr;
  Thread* idle = nullptr;
  Thread* prev = nullptr;  // от кого переключились — для finish_switch
  uint64_t switches = 0;
  bool initialized = false;
  Thread idle_thread = {};
};

constinit RunQueue g_rq[smp::kMaxCpus];

// Завершённые потоки, уже ушедшие с CPU: их структуры и стеки переиспользует
// spawn (освобождение стека потребовало бы TLB shootdown).
constinit sync::Spinlock g_zombie_lock{sync::LockRank::SchedZombie, "sched.zombies"};
constinit Thread* g_zombies = nullptr;

constinit uint64_t g_next_id = 1;
constinit uint64_t g_created = 0;

RunQueue& local_rq() { return g_rq[smp::this_cpu().index]; }

void enqueue(RunQueue& q, Thread* t) {
  t->next = nullptr;
  if (q.tail != nullptr) {
    q.tail->next = t;
  } else {
    q.head = t;
  }
  q.tail = t;
  ++q.ready;
}

Thread* dequeue(RunQueue& q) {
  Thread* t = q.head;
  if (t == nullptr) return nullptr;
  q.head = t->next;
  if (q.head == nullptr) q.tail = nullptr;
  --q.ready;
  t->next = nullptr;
  return t;
}

void remove_sleeper(RunQueue& q, Thread* t) {
  for (Thread** p = &q.sleepers; *p != nullptr; p = &(*p)->sleep_next) {
    if (*p == t) {
      *p = t->sleep_next;
      t->sleep_next = nullptr;
      return;
    }
  }
}

void kick_cpu(uint32_t cpu) {
  if (cpu != smp::this_cpu().index) {
    platform::local_apic().send_fixed(smp::cpu(cpu).apic_id, platform::kVectorWakeup);
  }
}

// Вызывается на стороне потока, на который только что переключились.
void finish_switch() {
  RunQueue& q = local_rq();
  Thread* p = q.prev;
  q.prev = nullptr;
  if (p == nullptr) return;
  __atomic_store_n(&p->on_cpu, false, __ATOMIC_RELEASE);
  if (p->state == State::Dead) {
    sync::SpinGuard guard(g_zombie_lock);
    p->next = g_zombies;
    g_zombies = p;
  }
}

uint32_t pick_cpu() {
  uint32_t best = 0;
  uint32_t best_load = ~0u;
  for (uint32_t i = 0; i < smp::cpu_count(); ++i) {
    if (!g_rq[i].initialized) continue;
    uint32_t load = __atomic_load_n(&g_rq[i].ready, __ATOMIC_RELAXED);
    if (load < best_load) {
      best = i;
      best_load = load;
    }
  }
  return best;
}

bool save_and_cli() {
  bool was = (arch::x86_64::read_rflags() & arch::x86_64::kRflagsIf) != 0;
  arch::x86_64::cli();
  return was;
}

void restore_if(bool was) {
  if (was) arch::x86_64::sti();
}

}  // namespace

void init_this_cpu(uint32_t tick_hz) {
  smp::Cpu& c = smp::this_cpu();
  RunQueue& q = g_rq[c.index];
  Thread& idle = q.idle_thread;
  idle.id = 0;
  idle.name = "idle";
  idle.state = State::Running;
  idle.cpu = c.index;
  idle.on_cpu = true;
  idle.is_idle = true;
  q.idle = &idle;
  q.current = &idle;
  __atomic_store_n(&q.initialized, true, __ATOMIC_RELEASE);
  platform::start_local_tick(tick_hz);
}

void idle_loop() {
  for (;;) {
    arch::x86_64::cli();
    RunQueue& q = local_rq();
    bool work;
    {
      sync::SpinGuard guard(q.lock);
      work = q.ready > 0;
    }
    if (work) {
      schedule();
    } else {
      // sti; hlt атомарны: прерывание между ними не потеряется.
      asm volatile("sti; hlt" ::: "memory");
    }
  }
}

void schedule() {
  smp::Cpu& c = smp::this_cpu();
  if (c.held_count != 0) panic("sched: switching while holding spinlock {}", c.held[0]->name());
  RunQueue& q = g_rq[c.index];
  Thread* prev;
  Thread* next;
  {
    sync::SpinGuard guard(q.lock);
    prev = q.current;
    if (prev->state == State::Running && !prev->is_idle) {
      prev->state = State::Ready;
      enqueue(q, prev);
    }
    next = dequeue(q);
    if (next == nullptr) next = prev->is_idle && prev->state == State::Running ? prev : q.idle;
    next->state = State::Running;
    if (next == prev) return;
    next->on_cpu = true;
    q.current = next;
    q.prev = prev;
    ++q.switches;
  }
  // Состояние x87/SSE: ядро его не трогает (-mgeneral-regs-only), поэтому
  // регистры принадлежат последнему пользовательскому потоку на этом CPU.
  if (prev->fpu != nullptr) asm volatile("fxsave64 (%0)" : : "r"(prev->fpu) : "memory");
  if (next->user_cr3 != 0) {
    // Вход из ring 3 (прерывание — TSS.RSP0, syscall — gs:8) попадает на стек
    // ядра этого потока.
    c.kernel_rsp = next->stack.top;
    arch::x86_64::set_kernel_entry_stack(c.descriptors, next->stack.top);
    if (arch::x86_64::read_cr3() != next->user_cr3) arch::x86_64::write_cr3(next->user_cr3);
    arch::x86_64::set_fs_base(next->fs_base);  // ядро FS не использует
  }
  if (next->fpu != nullptr) asm volatile("fxrstor64 (%0)" : : "r"(next->fpu) : "memory");
  switch_context(&prev->rsp, next->rsp);
  finish_switch();
}

void detach_user_context() {
  bool irq = save_and_cli();
  Thread* me = local_rq().current;
  me->fpu = nullptr;
  me->process = nullptr;
  me->user_cr3 = 0;
  me->user_obj = nullptr;
  me->fs_base = 0;
  restore_if(irq);
}

Result<Thread*, SchedError> spawn(const char* name, ThreadFn fn, void* arg, int cpu,
                                  const UserContext* user) {
  using R = Result<Thread*, SchedError>;
  uint32_t target;
  if (cpu == kAnyCpu) {
    target = pick_cpu();
  } else {
    if (cpu < 0 || static_cast<uint32_t>(cpu) >= smp::cpu_count()) return R::err(SchedError::BadCpu);
    target = static_cast<uint32_t>(cpu);
    if (!__atomic_load_n(&g_rq[target].initialized, __ATOMIC_ACQUIRE)) {
      return R::err(SchedError::BadCpu);
    }
  }

  Thread* t = nullptr;
  {
    sync::SpinGuard guard(g_zombie_lock);
    if (g_zombies != nullptr) {
      t = g_zombies;
      g_zombies = t->next;
    }
  }
  if (t == nullptr) {
    auto mem = mm::kmalloc(sizeof(Thread));
    if (mem.is_err()) return R::err(SchedError::OutOfMemory);
    auto stack = mm::alloc_kernel_stack();
    if (stack.is_err()) {
      mm::kfree(mem.value());
      return R::err(SchedError::OutOfMemory);
    }
    t = static_cast<Thread*>(mem.value());
    *t = Thread{};
    t->stack = stack.value();
  }
  mm::KernelStack stack = t->stack;
  *t = Thread{};
  t->stack = stack;
  t->id = __atomic_fetch_add(&g_next_id, 1, __ATOMIC_RELAXED);
  t->name = name;
  t->entry = fn;
  t->arg = arg;
  t->cpu = target;
  if (user != nullptr) {
    t->user_cr3 = user->cr3;
    t->process = user->process;
    t->fpu = user->fpu;
    t->user_obj = user->user_obj;
  }

  // Начальный стек для switch_context: 6 сохраняемых регистров, затем адрес
  // возврата thread_start. После ret RSP = top - 16, кратен 16.
  auto* sp = reinterpret_cast<uint64_t*>(stack.top - 72);
  for (int i = 0; i < 6; ++i) sp[i] = 0;
  sp[6] = reinterpret_cast<uint64_t>(&thread_start);
  t->rsp = reinterpret_cast<uint64_t>(sp);

  {
    sync::SpinGuard guard(g_rq[target].lock);
    t->state = State::Ready;
    enqueue(g_rq[target], t);
  }
  __atomic_fetch_add(&g_created, 1, __ATOMIC_RELAXED);
  kick_cpu(target);
  return R::ok(t);
}

Thread* current() { return local_rq().current; }

void yield() {
  bool irq = save_and_cli();
  schedule();
  restore_if(irq);
}

void sleep_ns(uint64_t ns) {
  if (ns == 0) {
    yield();
    return;
  }
  bool irq = save_and_cli();
  RunQueue& q = local_rq();
  {
    sync::SpinGuard guard(q.lock);
    Thread* me = q.current;
    me->state = State::Sleeping;
    me->wake_ns = platform::now_ns() + ns;
    Thread** p = &q.sleepers;
    while (*p != nullptr && (*p)->wake_ns <= me->wake_ns) p = &(*p)->sleep_next;
    me->sleep_next = *p;
    *p = me;
  }
  schedule();
  restore_if(irq);
}

void exit_thread() {
  arch::x86_64::cli();
  RunQueue& q = local_rq();
  {
    sync::SpinGuard guard(q.lock);
    q.current->state = State::Dead;
  }
  schedule();
  panic("sched: dead thread resumed");
}

void block_current() {
  RunQueue& q = local_rq();
  sync::SpinGuard guard(q.lock);
  q.current->state = State::Blocked;
}

void block_current_until(uint64_t deadline_ns) {
  if (deadline_ns == ~uint64_t{0}) {
    block_current();
    return;
  }
  RunQueue& q = local_rq();
  sync::SpinGuard guard(q.lock);
  Thread* me = q.current;
  me->state = State::Sleeping;
  me->wake_ns = deadline_ns;
  Thread** p = &q.sleepers;
  while (*p != nullptr && (*p)->wake_ns <= deadline_ns) p = &(*p)->sleep_next;
  me->sleep_next = *p;
  *p = me;
}

void wake(Thread* t) {
  uint32_t target = t->cpu;
  bool woke = false;
  {
    sync::SpinGuard guard(g_rq[target].lock);
    if (t->state == State::Blocked || t->state == State::Sleeping) {
      if (t->state == State::Sleeping) remove_sleeper(g_rq[target], t);
      if (t == g_rq[target].current) {
        // Поток ещё не ушёл с CPU (wake между block_current и schedule):
        // возвращаем Running — schedule поставит его в очередь, как при
        // обычном вытеснении. Пометка Ready без постановки в очередь
        // потеряла бы поток.
        t->state = State::Running;
      } else {
        t->state = State::Ready;
        enqueue(g_rq[target], t);
      }
      woke = true;
    }
  }
  if (woke) kick_cpu(target);
}

void interrupt(Thread* t) {
  __atomic_store_n(&t->interrupt_pending, true, __ATOMIC_SEQ_CST);
  if (__atomic_load_n(&t->interruptible, __ATOMIC_SEQ_CST)) wake(t);
}

bool interrupted() { return __atomic_load_n(&local_rq().current->interrupt_pending, __ATOMIC_ACQUIRE); }

void block_interruptible_until(uint64_t deadline_ns) {
  Thread* me = local_rq().current;
  __atomic_store_n(&me->interruptible, true, __ATOMIC_SEQ_CST);
  block_current_until(deadline_ns);
  // interrupt() мог прийти до того, как мы стали прерываемыми: тогда он нас
  // не будил — будим себя сами (wake до schedule безопасен).
  if (__atomic_load_n(&me->interrupt_pending, __ATOMIC_SEQ_CST)) wake(me);
}

void end_interruptible() { __atomic_store_n(&local_rq().current->interruptible, false, __ATOMIC_RELEASE); }

void on_timer_tick() {
  RunQueue& q = local_rq();
  if (!__atomic_load_n(&q.initialized, __ATOMIC_ACQUIRE)) return;
  uint64_t now = platform::now_ns();
  bool resched;
  {
    sync::SpinGuard guard(q.lock);
    while (q.sleepers != nullptr && q.sleepers->wake_ns <= now) {
      Thread* t = q.sleepers;
      q.sleepers = t->sleep_next;
      t->sleep_next = nullptr;
      t->state = State::Ready;
      enqueue(q, t);
    }
    resched = q.ready > 0;
  }
  if (resched) schedule();
}

uint64_t context_switches(uint32_t cpu) { return g_rq[cpu].switches; }
uint64_t threads_created() { return g_created; }

}  // namespace kernel::sched

extern "C" void thread_main() {
  using namespace kernel::sched;
  finish_switch();
  Thread* t = current();
  arch::x86_64::sti();
  t->entry(t->arg);
  exit_thread();
}
