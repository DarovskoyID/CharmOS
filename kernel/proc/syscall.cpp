#include "../console/console.hpp"
#include "../fs/vfs.hpp"
#include "../net/socket.hpp"
#include "syscall.hpp"

#include <abi/syscall.hpp>
#include <arch/x86_64/cpu.hpp>
#include <arch/x86_64/gdt.hpp>

#include "../core/log.hpp"
#include "../ipc/ipc.hpp"
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
  if (deadline <= now) return set_result(f, Status::Ok);
  // Прерываемый сон (process.md §9): убийство процесса будит сразу.
  bool irq = (arch::x86_64::read_rflags() & arch::x86_64::kRflagsIf) != 0;
  arch::x86_64::cli();
  if (!sched::interrupted()) {
    sched::block_interruptible_until(deadline);
    sched::schedule();
    sched::end_interruptible();
  }
  if (irq) arch::x86_64::sti();
  set_result(f, sched::interrupted() && platform::now_ns() < deadline ? Status::Interrupted : Status::Ok);
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
    case Number::HandleClose:
      set_result(f, kernel::ipc::sys_handle_close(static_cast<uint32_t>(f.rdi)));
      break;
    case Number::HandleDuplicate: {
      uint64_t v = 0;
      set_result(f, kernel::ipc::sys_handle_duplicate(static_cast<uint32_t>(f.rdi), f.rsi, &v), v);
      break;
    }
    case Number::HandleRevoke: {
      uint64_t v = 0;
      set_result(f, kernel::ipc::sys_handle_revoke(static_cast<uint32_t>(f.rdi), &v), v);
      break;
    }
    case Number::ChannelCreate:
      set_result(f, kernel::ipc::sys_channel_create(f.rdi));
      break;
    case Number::ChannelWrite:
      set_result(f, kernel::ipc::sys_channel_write(static_cast<uint32_t>(f.rdi), f.rsi, f.rdx,
                                                   f.r10, f.r8));
      break;
    case Number::ChannelRead: {
      uint64_t v = 0;
      Status s = kernel::ipc::sys_channel_read(static_cast<uint32_t>(f.rdi), f.rsi, f.rdx, f.r10,
                                               f.r8, f.r9, &v);
      set_result(f, s, v);
      break;
    }
    case Number::VmoCreate: {
      uint64_t v = 0;
      set_result(f, kernel::ipc::sys_vmo_create(f.rdi, &v), v);
      break;
    }
    case Number::VmoMap: {
      uint64_t v = 0;
      set_result(f, kernel::ipc::sys_vmo_map(static_cast<uint32_t>(f.rdi), f.rsi, &v), v);
      break;
    }
    case Number::VmoUnmap:
      set_result(f, kernel::ipc::sys_vmo_unmap(f.rdi));
      break;
    case Number::PortCreate: {
      uint64_t v = 0;
      set_result(f, kernel::ipc::sys_port_create(&v), v);
      break;
    }
    case Number::PortWait:
      set_result(f, kernel::ipc::sys_port_wait(static_cast<uint32_t>(f.rdi), f.rsi, f.rdx));
      break;
    case Number::DeviceInfo:
      set_result(f, kernel::ipc::sys_device_info(static_cast<uint32_t>(f.rdi), f.rsi));
      break;
    case Number::DeviceMapBar: {
      uint64_t v = 0;
      set_result(f, kernel::ipc::sys_device_map_bar(static_cast<uint32_t>(f.rdi), f.rsi, &v), v);
      break;
    }
    case Number::DeviceIrq: {
      uint64_t v = 0;
      set_result(f, kernel::ipc::sys_device_irq(static_cast<uint32_t>(f.rdi), f.rsi, &v), v);
      break;
    }
    case Number::InterruptAck:
      set_result(f, kernel::ipc::sys_interrupt_ack(static_cast<uint32_t>(f.rdi)));
      break;
    case Number::DmaAlloc: {
      uint64_t v = 0;
      set_result(f, kernel::ipc::sys_dma_alloc(static_cast<uint32_t>(f.rdi), f.rsi, f.rdx, f.r10,
                                               &v), v);
      break;
    }
    case Number::DeviceVendorCap: {
      uint64_t v = 0;
      set_result(f, kernel::ipc::sys_device_vendor_cap(static_cast<uint32_t>(f.rdi), f.rsi, f.rdx,
                                                       f.r10, &v), v);
      break;
    }
    case Number::FsOpen: {
      uint64_t v = 0;
      set_result(f, kernel::fs::sys_fs_open(static_cast<uint32_t>(f.rdi), f.rsi, f.rdx, f.r10, &v), v);
      break;
    }
    case Number::FileRead: {
      uint64_t v = 0;
      set_result(f, kernel::fs::sys_file_read(static_cast<uint32_t>(f.rdi), f.rsi, f.rdx, f.r10, &v), v);
      break;
    }
    case Number::FileWrite: {
      uint64_t v = 0;
      set_result(f, kernel::fs::sys_file_write(static_cast<uint32_t>(f.rdi), f.rsi, f.rdx, f.r10, &v), v);
      break;
    }
    case Number::FileSync:
      set_result(f, kernel::fs::sys_file_sync(static_cast<uint32_t>(f.rdi)));
      break;
    case Number::FsStat:
      set_result(f, kernel::fs::sys_fs_stat(static_cast<uint32_t>(f.rdi), f.rsi));
      break;
    case Number::DirRead: {
      uint64_t v = 0;
      set_result(f, kernel::fs::sys_dir_read(static_cast<uint32_t>(f.rdi), f.rsi, f.rdx, f.r10, &v), v);
      break;
    }
    case Number::FsUnlink:
      set_result(f, kernel::fs::sys_fs_unlink(static_cast<uint32_t>(f.rdi), f.rsi, f.rdx));
      break;
    case Number::FileResize:
      set_result(f, kernel::fs::sys_file_resize(static_cast<uint32_t>(f.rdi), f.rsi));
      break;
    case Number::SocketOpen: {
      uint64_t v = 0;
      set_result(f, kernel::net::sys_socket_open(static_cast<uint32_t>(f.rdi), f.rsi, &v), v);
      break;
    }
    case Number::SocketBind:
      set_result(f, kernel::net::sys_socket_bind(static_cast<uint32_t>(f.rdi), f.rsi));
      break;
    case Number::SocketListen:
      set_result(f, kernel::net::sys_socket_listen(static_cast<uint32_t>(f.rdi), f.rsi));
      break;
    case Number::SocketAccept: {
      uint64_t v = 0;
      set_result(f, kernel::net::sys_socket_accept(static_cast<uint32_t>(f.rdi), f.rsi, f.rdx, &v), v);
      break;
    }
    case Number::SocketConnect:
      set_result(f, kernel::net::sys_socket_connect(static_cast<uint32_t>(f.rdi), f.rsi, f.rdx));
      break;
    case Number::SocketSend: {
      uint64_t v = 0;
      set_result(f, kernel::net::sys_socket_send(static_cast<uint32_t>(f.rdi), f.rsi, f.rdx, f.r10, &v), v);
      break;
    }
    case Number::SocketRecv: {
      uint64_t v = 0;
      set_result(f, kernel::net::sys_socket_recv(static_cast<uint32_t>(f.rdi), f.rsi, f.rdx, f.r10, f.r8, &v), v);
      break;
    }
    case Number::SocketShutdown:
      set_result(f, kernel::net::sys_socket_shutdown(static_cast<uint32_t>(f.rdi), f.rsi));
      break;
    case Number::NetInfo:
      set_result(f, kernel::net::sys_net_info(static_cast<uint32_t>(f.rdi), f.rsi));
      break;
    case Number::DnsResolve: {
      uint64_t v = 0;
      set_result(f, kernel::net::sys_dns_resolve(static_cast<uint32_t>(f.rdi), f.rsi, f.rdx, f.r10, &v), v);
      break;
    }
    case Number::ProcessSpawn: {
      uint64_t v = 0;
      set_result(f, sys_process_spawn(f.rdi, &v), v);
      break;
    }
    case Number::ProcessKill:
      set_result(f, sys_process_kill(static_cast<uint32_t>(f.rdi)));
      break;
    case Number::ProcessInfo:
      set_result(f, sys_process_info(static_cast<uint32_t>(f.rdi), f.rsi));
      break;
    case Number::JobCreate: {
      uint64_t v = 0;
      set_result(f, kernel::ipc::sys_job_create(static_cast<uint32_t>(f.rdi), &v), v);
      break;
    }
    case Number::JobKill:
      set_result(f, sys_job_kill(static_cast<uint32_t>(f.rdi)));
      break;
    case Number::JobInfo:
      set_result(f, kernel::ipc::sys_job_info(static_cast<uint32_t>(f.rdi), f.rsi));
      break;
    case Number::ConsoleSetForeground:
      set_result(f, sys_console_set_foreground(static_cast<uint32_t>(f.rdi), static_cast<uint32_t>(f.rsi)));
      break;
    case Number::ThreadCreate: {
      uint64_t v = 0;
      set_result(f, sys_thread_create(f.rdi, f.rsi, f.rdx, &v), v);
      break;
    }
    case Number::ThreadExit:
      exit_current_thread(static_cast<int64_t>(f.rdi));
    case Number::ThreadInfo:
      set_result(f, kernel::ipc::sys_thread_info(static_cast<uint32_t>(f.rdi), f.rsi));
      break;
    case Number::FutexWait:
      set_result(f, sys_futex_wait(f.rdi, f.rsi, f.rdx));
      break;
    case Number::FutexWake: {
      uint64_t v = 0;
      set_result(f, sys_futex_wake(f.rdi, f.rsi, &v), v);
      break;
    }
    case Number::ThreadSetFs:
      set_result(f, sys_thread_set_fs(f.rdi));
      break;
    case Number::ConsoleRead: {
      uint64_t v = 0;
      set_result(f, kernel::console::sys_console_read(static_cast<uint32_t>(f.rdi), f.rsi, f.rdx, f.r10, &v), v);
      break;
    }
    case Number::ConsoleWrite: {
      uint64_t v = 0;
      set_result(f, kernel::console::sys_console_write(static_cast<uint32_t>(f.rdi), f.rsi, f.rdx, &v), v);
      break;
    }
    case Number::ObjectBind:
      set_result(f, kernel::ipc::sys_object_bind(static_cast<uint32_t>(f.rdi),
                                                 static_cast<uint32_t>(f.rsi), f.rdx));
      break;
    default:
      set_result(f, Status::NotSupported);
      break;
  }
  // SYSRET с неканоническим RCX бросает #GP уже в ring 0, но со стеком
  // пользователя (SDM Vol. 2B, SYSRET). Адрес возврата из нижней половины
  // каноничен; иное возможно, только если SYSCALL стоял в самом конце нижней
  // половины, — такой процесс завершаем.
  check_kill();
  if (f.rip >= abi::syscall::kUserTop) {
    kernel::kprint("proc: {} returned to non-canonical rip {X}, killed\n",
                   current_process()->name, f.rip);
    exit_current(kExitKilled);
  }
  f.rflags = (f.rflags & ~kFlagsUserForbidden) | kFlagIf;
}
