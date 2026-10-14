#include "syscall.hpp"

#include <abi/syscall.hpp>
#include <arch/x86_64/cpu.hpp>
#include <arch/x86_64/gdt.hpp>

#include "../core/log.hpp"
#include "../platform/platform.hpp"
#include "../sched/sched.hpp"
#include "process.hpp"
#include "uaccess.hpp"

extern "C" void syscall_entry();

namespace kernel::proc {

using abi::syscall::Number;
using abi::syscall::Status;

namespace {

// Адреса MSR (SDM Vol. 4, табл. 2-2).
constexpr uint32_t kMsrStar = 0xc0000081;
constexpr uint32_t kMsrLstar = 0xc0000082;
constexpr uint32_t kMsrFmask = 0xc0000084;
constexpr uint64_t kEferSce = 1ull << 0;

// Флаги, которые SYSCALL сбрасывает на входе: TF, IF, DF, NT, AC.
constexpr uint64_t kFmask = (1ull << 8) | (1ull << 9) | (1ull << 10) | (1ull << 14) | (1ull << 18);
// Флаги, которые пользователь не может установить через возврат: IOPL, NT,
// RF, VM, VIF, VIP. IF всегда 1 (docs/abi/syscalls.md §2).
constexpr uint64_t kFlagsUserForbidden = (3ull << 12) | (1ull << 14) | (1ull << 16) |
                                         (1ull << 17) | (1ull << 19) | (1ull << 20);
constexpr uint64_t kFlagIf = 1ull << 9;

void set_result(SyscallFrame& f, Status s, uint64_t value = 0) {
  f.rax = static_cast<uint64_t>(s);
  f.rdx = value;
}

void sys_debug_write(SyscallFrame& f) {
  uint64_t len = f.rsi;
  if (len > abi::syscall::kDebugWriteMax) return set_result(f, Status::InvalidArgs);
  char buf[abi::syscall::kDebugWriteMax];
  if (!copy_from_user(buf, f.rdi, len)) return set_result(f, Status::BadAddress);
  Process* p = current_process();
  kprint("[{}] {}", p->name, lib::core::StringView(buf, len));
  set_result(f, Status::Ok, len);
}

void sys_nanosleep(SyscallFrame& f) {
  uint64_t deadline = f.rdi;
  uint64_t now = platform::now_ns();
  if (deadline > now) sched::sleep_ns(deadline - now);
  set_result(f, Status::Ok);
}

}  // namespace

void init_syscall_this_cpu() {
  using namespace arch::x86_64;
  wrmsr(kMsrEfer, rdmsr(kMsrEfer) | kEferSce);
  // STAR[47:32] — CS ядра (SS = +8), STAR[63:48] — база для возврата в
  // 64-битный ring 3: CS = база + 16, SS = база + 8 (SDM §5.8.8). При базе
  // 0x10 это 0x20 (код пользователя) и 0x18 (данные), см. gdt.hpp.
  wrmsr(kMsrStar, (uint64_t{kKernelData} << 48) | (uint64_t{kKernelCode} << 32));
  wrmsr(kMsrLstar, reinterpret_cast<uint64_t>(&syscall_entry));
  wrmsr(kMsrFmask, kFmask);
}

}  // namespace kernel::proc

extern "C" void syscall_dispatch(kernel::proc::SyscallFrame* frame) {
  using namespace kernel::proc;
  SyscallFrame& f = *frame;
  switch (static_cast<Number>(f.rax)) {
    case Number::AbiVersion:
      set_result(f, Status::Ok,
                 (uint64_t{abi::syscall::kVersionMajor} << 16) | abi::syscall::kVersionMinor);
      break;
    case Number::DebugWrite:
      sys_debug_write(f);
      break;
    case Number::ProcessExit:
      exit_current(static_cast<int64_t>(f.rdi));
    case Number::ThreadYield:
      kernel::sched::yield();
      set_result(f, Status::Ok);
      break;
    case Number::ClockMonotonic:
      set_result(f, Status::Ok, kernel::platform::now_ns());
      break;
    case Number::Nanosleep:
      sys_nanosleep(f);
      break;
    default:
      set_result(f, Status::NotSupported);
      break;
  }
  // SYSRET с неканоническим RCX бросает #GP уже в ring 0, но со стеком
  // пользователя (SDM Vol. 2B, SYSRET). Адрес возврата из нижней половины
  // каноничен; иное возможно, только если SYSCALL стоял в самом конце нижней
  // половины, — такой процесс завершаем.
  if (f.rip >= abi::syscall::kUserTop) {
    kernel::kprint("proc: {} returned to non-canonical rip {X}, killed\n",
                   current_process()->name, f.rip);
    exit_current(kExitKilled);
  }
  f.rflags = (f.rflags & ~kFlagsUserForbidden) | kFlagIf;
}
