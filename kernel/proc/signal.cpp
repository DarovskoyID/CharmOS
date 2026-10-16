// Сигналы (docs/abi/process.md §9, include/abi/signal.hpp).
//
// Ожидающие сигналы — битовая маска процесса; маска блокировки — у потока.
// Отправка ставит бит и прерывает один поток, у которого сигнал не
// заблокирован (его блокирующий вызов возвращает Interrupted). Доставка —
// при возврате в ring 3: из системного вызова, по тику таймера, по IPI
// пробуждения. Fault — синхронно, из обработчика исключения.

#include <abi/process.hpp>
#include <abi/signal.hpp>
#include <abi/syscall.hpp>
#include <arch/x86_64/cpu.hpp>

#include "../core/log.hpp"
#include "../platform/platform.hpp"
#include "../sched/sched.hpp"
#include "process.hpp"
#include "signal.hpp"
#include "syscall.hpp"
#include "uaccess.hpp"

extern "C" [[noreturn]] void resume_user(const abi::signal::SignalFrame* frame);

namespace kernel::proc {

namespace {

namespace ap = abi::process;
using abi::signal::SignalFrame;
using abi::signal::SignalInfo;

constexpr uint64_t kAllSignals = ((1ull << (ap::kSigMax + 1)) - 1) & ~1ull;
// RFLAGS, которые обработчик может вернуть: CF PF AF ZF SF DF OF (SDM Vol. 1 §3.4.3).
constexpr uint64_t kUserFlags = 0xcd5;
constexpr uint64_t kFlagIf = 1ull << 9;
constexpr uint64_t kFlagReserved1 = 1ull << 1;

uint64_t bit(uint32_t signo) { return 1ull << signo; }

bool ignored_by_default(uint32_t signo) {
  return signo == ap::kSigChild || signo == ap::kSigAlarm || signo == ap::kSigUser1 || signo == ap::kSigUser2;
}

// Следующий доставляемый сигнал текущему потоку (бит снимается).
bool take(Process* p, sched::Thread* t, uint32_t* signo) {
  // Сначала снять, потом проверить exiting: begin_exit_locked ставит exiting до
  // interrupt(), поэтому убийство, пришедшее между нашими шагами, не теряется
  // (иначе поток засыпает в следующем вызове навсегда — зависал svc poweroff).
  __atomic_store_n(&t->interrupt_pending, false, __ATOMIC_SEQ_CST);
  if (__atomic_load_n(&p->exiting, __ATOMIC_SEQ_CST)) {
    __atomic_store_n(&t->interrupt_pending, true, __ATOMIC_SEQ_CST);
  }
  for (;;) {
    uint64_t pending = __atomic_load_n(&p->sig_pending, __ATOMIC_ACQUIRE);
    uint64_t ready = pending & ~t->sig_mask & kAllSignals;
    if (ready == 0) return false;
    uint32_t s = static_cast<uint32_t>(__builtin_ctzll(ready));
    if (__atomic_compare_exchange_n(&p->sig_pending, &pending, pending & ~bit(s), false, __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE)) {
      *signo = s;
      return true;
    }
  }
}

// Кладёт кадр и сведения на стек сигналов. regs — состояние прерванного
// кода (без FPU: берётся из регистров). При успехе — новые RSP и адреса.
bool push_frame(Process* p, sched::Thread* t, SignalFrame& regs, const SignalInfo& info, uint64_t* rsp,
                uint64_t* frame_addr, uint64_t* info_addr) {
  uint64_t top = p->sig_stack_top;
  uint64_t lo = top - p->sig_stack_size;
  uint64_t sp = regs.rsp > lo && regs.rsp <= top ? regs.rsp - 128 : top;
  sp &= ~uint64_t{15};
  sp -= sizeof(SignalFrame);
  *frame_addr = sp;
  sp -= sizeof(SignalInfo);
  *info_addr = sp;
  sp -= 8;  // адрес возврата 0: RSP ≡ 8 (mod 16), как после call
  if (sp < lo || sp > top) return false;
  regs.mask = t->sig_mask;
  regs.link = t->sig_frame;
  arch::x86_64::fxsave(regs.fpu);
  uint64_t zero = 0;
  if (!copy_to_user(*frame_addr, &regs, sizeof regs) || !copy_to_user(*info_addr, &info, sizeof info) ||
      !copy_to_user(sp, &zero, sizeof zero)) {
    return false;
  }
  *rsp = sp;
  t->sig_frame = *frame_addr;
  ++t->sig_depth;
  return true;
}

[[noreturn]] void die_by_signal(Process* p, uint32_t signo) {
  kprint("proc: {} (pid {}) terminated by signal {}\n", p->name, p->pid, signo);
  exit_current(kExitKilled);
}

// Обработчик доставки: true — регистры для входа в обработчик заполнены.
template <typename Frame>
bool deliver(Frame& f, SignalFrame& regs) {
  Process* p = current_process();
  sched::Thread* t = sched::current();
  if (p == nullptr) return false;
  uint32_t signo;
  while (take(p, t, &signo)) {
    uint64_t entry = __atomic_load_n(&p->sig_entry, __ATOMIC_ACQUIRE);
    if (entry == 0) {
      if (ignored_by_default(signo)) continue;
      die_by_signal(p, signo);
    }
    SignalInfo info{signo, 0, 0, 0, 0};
    uint64_t rsp, frame_addr, info_addr;
    if (!push_frame(p, t, regs, info, &rsp, &frame_addr, &info_addr)) {
      kprint("proc: {} (pid {}) killed: bad signal stack\n", p->name, p->pid);
      exit_current(kExitKilled);
    }
    t->sig_mask |= bit(signo);
    f.rip = entry;
    f.rsp = rsp;
    f.rdi = signo;
    f.rsi = info_addr;
    f.rdx = frame_addr;
    f.rflags = kFlagIf | kFlagReserved1;
    return true;
  }
  return false;
}

}  // namespace

void post_signal(Process* p, uint32_t signo) {
  if (signo == 0 || signo > ap::kSigMax) return;
  if (__atomic_load_n(&p->exiting, __ATOMIC_ACQUIRE)) return;
  if (__atomic_load_n(&p->sig_entry, __ATOMIC_ACQUIRE) == 0 && ignored_by_default(signo)) return;
  __atomic_fetch_or(&p->sig_pending, bit(signo), __ATOMIC_ACQ_REL);
  sync::SpinGuard guard(p->threads_lock);
  for (sched::Thread* t = p->threads; t != nullptr; t = t->proc_next) {
    if ((t->sig_mask & bit(signo)) == 0) {
      sched::interrupt(t);
      return;
    }
  }
}

void deliver_signals(SyscallFrame& f) {
  SignalFrame regs{};
  regs.rax = f.rax;
  regs.rbx = f.rbx;
  regs.rcx = f.rip;  // SYSCALL портит RCX и R11: после sysret в них были бы RIP и RFLAGS
  regs.rdx = f.rdx;
  regs.rsi = f.rsi;
  regs.rdi = f.rdi;
  regs.rbp = f.rbp;
  regs.rsp = f.rsp;
  regs.r8 = f.r8;
  regs.r9 = f.r9;
  regs.r10 = f.r10;
  regs.r11 = f.rflags;
  regs.r12 = f.r12;
  regs.r13 = f.r13;
  regs.r14 = f.r14;
  regs.r15 = f.r15;
  regs.rip = f.rip;
  regs.rflags = f.rflags;
  (void)deliver(f, regs);
}

void deliver_signals(arch::x86_64::InterruptFrame& f) {
  if ((f.cs & 3) != 3) return;
  SignalFrame regs{};
  regs.rax = f.rax;
  regs.rbx = f.rbx;
  regs.rcx = f.rcx;
  regs.rdx = f.rdx;
  regs.rsi = f.rsi;
  regs.rdi = f.rdi;
  regs.rbp = f.rbp;
  regs.rsp = f.rsp;
  regs.r8 = f.r8;
  regs.r9 = f.r9;
  regs.r10 = f.r10;
  regs.r11 = f.r11;
  regs.r12 = f.r12;
  regs.r13 = f.r13;
  regs.r14 = f.r14;
  regs.r15 = f.r15;
  regs.rip = f.rip;
  regs.rflags = f.rflags;
  (void)deliver(f, regs);
}

bool deliver_fault(arch::x86_64::InterruptFrame& f) {
  Process* p = current_process();
  sched::Thread* t = sched::current();
  if (p == nullptr) return false;
  uint64_t entry = __atomic_load_n(&p->sig_entry, __ATOMIC_ACQUIRE);
  bool fault_vector = f.vector == arch::x86_64::kVectorDivideError || f.vector == arch::x86_64::kVectorInvalidOpcode ||
                      f.vector == arch::x86_64::kVectorGeneralProtection ||
                      f.vector == arch::x86_64::kVectorPageFault;
  // Повторный Fault внутри обработчика или заблокированный Fault — смерть.
  if (entry == 0 || !fault_vector || t->sig_depth != 0 || (t->sig_mask & bit(ap::kSigFault)) != 0) return false;
  SignalFrame regs{};
  regs.rax = f.rax;
  regs.rbx = f.rbx;
  regs.rcx = f.rcx;
  regs.rdx = f.rdx;
  regs.rsi = f.rsi;
  regs.rdi = f.rdi;
  regs.rbp = f.rbp;
  regs.rsp = f.rsp;
  regs.r8 = f.r8;
  regs.r9 = f.r9;
  regs.r10 = f.r10;
  regs.r11 = f.r11;
  regs.r12 = f.r12;
  regs.r13 = f.r13;
  regs.r14 = f.r14;
  regs.r15 = f.r15;
  regs.rip = f.rip;
  regs.rflags = f.rflags;
  SignalInfo info{ap::kSigFault, static_cast<uint32_t>(f.vector),
                  f.vector == arch::x86_64::kVectorPageFault ? arch::x86_64::read_cr2() : f.rip, f.error_code, 0};
  uint64_t rsp, frame_addr, info_addr;
  if (!push_frame(p, t, regs, info, &rsp, &frame_addr, &info_addr)) return false;
  t->sig_mask |= bit(ap::kSigFault);
  f.rip = entry;
  f.rsp = rsp;
  f.rdi = ap::kSigFault;
  f.rsi = info_addr;
  f.rdx = frame_addr;
  f.rflags = kFlagIf | kFlagReserved1;
  return true;
}

// --- системные вызовы 59–63 -------------------------------------------------------------

Status sys_signal_send(uint32_t h, uint64_t signo) {
  if (signo == 0 || signo > ap::kSigMax) return Status::InvalidArgs;
  Process* p;
  Status s = ipc::get_process(h, abi::syscall::rights::kSignal, &p);
  if (s != Status::Ok) return s;
  post_signal(p, static_cast<uint32_t>(signo));
  put_process(p);
  return Status::Ok;
}

Status sys_signal_set_handler(uint64_t entry, uint64_t stack_top, uint64_t stack_size) {
  Process* p = current_process();
  if (entry == 0) {
    __atomic_store_n(&p->sig_entry, 0, __ATOMIC_RELEASE);
    return Status::Ok;
  }
  if (entry < kUserImageMin || entry >= abi::syscall::kUserTop || stack_top > abi::syscall::kUserTop ||
      stack_size > stack_top || stack_top - stack_size < kUserImageMin) {
    return Status::BadAddress;
  }
  if (stack_top % 16 != 0 || stack_size < abi::signal::kMinSignalStack) return Status::InvalidArgs;
  p->sig_stack_top = stack_top;
  p->sig_stack_size = stack_size;
  __atomic_store_n(&p->sig_entry, entry, __ATOMIC_RELEASE);
  return Status::Ok;
}

Status sys_signal_mask(uint64_t block, uint64_t unblock, uint64_t* old) {
  sched::Thread* t = sched::current();
  *old = t->sig_mask;
  t->sig_mask = (t->sig_mask | (block & kAllSignals)) & ~unblock;
  return Status::Ok;  // разблокированные ожидающие доставит выход из вызова
}

Status sys_signal_alarm(uint64_t deadline) {
  __atomic_store_n(&current_process()->alarm_ns, deadline, __ATOMIC_RELEASE);
  return Status::Ok;
}

void sys_signal_return() {
  Process* p = current_process();
  sched::Thread* t = sched::current();
  if (t->sig_frame == 0) return;  // не в обработчике: вызов возвращает InvalidArgs
  SignalFrame f;
  if (!copy_from_user(&f, t->sig_frame, sizeof f) || f.rip >= abi::syscall::kUserTop ||
      f.rsp >= abi::syscall::kUserTop) {
    kprint("proc: {} (pid {}) killed: bad signal frame\n", p->name, p->pid);
    exit_current(kExitKilled);
  }
  f.rflags = (f.rflags & kUserFlags) | kFlagIf | kFlagReserved1;
  arch::x86_64::sanitize_fxsave(f.fpu);
  t->sig_frame = f.link;
  t->sig_mask = f.mask & kAllSignals;
  if (t->sig_depth != 0) --t->sig_depth;
  arch::x86_64::cli();
  arch::x86_64::fxrstor(f.fpu);
  resume_user(&f);
}

}  // namespace kernel::proc
