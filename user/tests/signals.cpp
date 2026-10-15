// Сигналы, лимиты и приоритеты (docs/abi/process.md §7–§9):
//  - Alarm в обработчик посреди вычислений (FPU и регистры не портятся);
//  - Alarm прерывает futex_wait без срока: Interrupted после обработчика;
//  - заблокированный сигнал ждёт и приходит при разблокировке;
//  - Fault: обработчик пропускает инструкцию, меняя кадр;
//  - signal_send ребёнку: Terminate без обработчика — смерть (-1000),
//    User1 без обработчика игнорируется; Child приходит родителю;
//  - лимиты Job: processes (NoSpace), cpu_ns (job_kill);
//  - thread_set_priority.
#include <abi/signal.hpp>
#include <charm/start.hpp>

#include "check.hpp"

using namespace charm;
namespace ap = abi::process;
using abi::signal::SignalFrame;
using abi::signal::SignalInfo;

namespace {

char g_buf[sizeof(ap::StartHeader) + 4096 + 8192];
volatile uint32_t g_count[ap::kSigMax + 1] = {};
volatile uint64_t g_fault_addr = 1;
volatile uint32_t g_fault_code = 0;
volatile uint32_t g_never = 0;

inline SysResult sig_set_handler(void (*entry)(uint64_t, SignalInfo*, SignalFrame*), uint64_t top, uint64_t size) {
  return syscall3(Number::SignalSetHandler, reinterpret_cast<uint64_t>(entry), top, size);
}
inline SysResult sig_mask(uint64_t block, uint64_t unblock) { return syscall2(Number::SignalMask, block, unblock); }
inline Status sig_alarm(uint64_t deadline) { return syscall1(Number::SignalAlarm, deadline).status; }
inline Status sig_send(Handle p, uint64_t signo) { return syscall2(Number::SignalSend, p, signo).status; }
inline Status job_set_limits(Handle j, const ap::JobLimits* l) {
  return syscall2(Number::JobSetLimits, j, reinterpret_cast<uint64_t>(l)).status;
}
inline Status thread_set_priority(Handle t, uint64_t prio) { return syscall2(Number::ThreadSetPriority, t, prio).status; }

void handler(uint64_t signo, SignalInfo* info, SignalFrame* frame) {
  if (info->signo != signo) process_exit(200);
  // Плавающая точка в обработчике: прерванный код её не заметит.
  volatile double x = 1.5;
  for (int i = 0; i < 100; ++i) x = x * 1.01;
  if (signo == ap::kSigFault) {
    g_fault_code = info->code;
    g_fault_addr = info->addr;
    frame->rip += 3;  // mov (%rax), %rax
    frame->rax = 77;
  }
  g_count[signo] = g_count[signo] + 1;
  syscall0(Number::SignalReturn);
  process_exit(201);  // signal_return не вернулся бы сюда
}

uint64_t new_stack(uint64_t size) {
  auto v = vmo_create(size);
  if (!v.ok()) return 0;
  auto m = vmo_map(static_cast<Handle>(v.value), rights::kRead | rights::kWrite);
  return m.ok() ? m.value + size : 0;
}

SysResult spawn_child(Handle exe, const char* args, uint32_t bytes, Handle job) {
  ap::SpawnArgs a{};
  a.executable = exe;
  a.args = args;
  a.args_bytes = bytes;
  a.argc = 2;
  a.name = "child";
  a.name_len = 5;
  a.job = job;
  return process_spawn(&a);
}

bool wait_packet(Handle port, uint64_t key, uint32_t type, uint64_t* data0) {
  PortPacket pkt;
  Status s;
  do {
    s = port_wait(port, &pkt, now_ns() + 30'000'000'000ull);
  } while (s == Status::Interrupted);  // сигнал Child посреди ожидания
  if (s != Status::Ok) return false;
  *data0 = pkt.data0;
  return pkt.key == key && pkt.type == type;
}

void wait_count(uint32_t signo, uint32_t want) {
  uint64_t until = now_ns() + 5'000'000'000ull;
  while (g_count[signo] < want && now_ns() < until) yield();
}

__attribute__((force_align_arg_pointer)) void idle_thread(uint64_t) {
  (void)futex_wait(&g_never, 0, now_ns() + 200'000'000);
  thread_exit(0);
}

const char kSleep[] = "spawn_child\0sleep";
const char kSpin[] = "spawn_child\0spin";

}  // namespace

extern "C" int64_t charm_main(Handle boot) {
  Start st;
  EXPECT(read_start(boot, g_buf, sizeof g_buf, &st) == Status::Ok);
  Handle root = st.find(ap::kRoleRoot);
  Handle job = st.find(ap::kRoleJob);
  EXPECT(root != 0 && job != 0);

  uint64_t stack = new_stack(64 * 1024);
  EXPECT(stack != 0);
  EXPECT(sig_set_handler(handler, stack, 64 * 1024).status == Status::Ok);
  EXPECT(sig_set_handler(handler, stack + 8, 64 * 1024).status == Status::InvalidArgs);

  // 1. Alarm посреди вычислений.
  double acc = 0.0;
  EXPECT(sig_alarm(now_ns() + 30'000'000) == Status::Ok);
  uint64_t until = now_ns() + 5'000'000'000ull;
  int64_t n = 0;
  while (g_count[ap::kSigAlarm] == 0 && now_ns() < until) {
    for (int i = 0; i < 1000; ++i) acc += 0.25;
    n += 1000;
  }
  EXPECT(g_count[ap::kSigAlarm] == 1);
  EXPECT(acc == static_cast<double>(n) * 0.25);
  print("signals: alarm handled\n");

  // 2. Блокирующий вызов прерывается.
  EXPECT(sig_alarm(now_ns() + 30'000'000) == Status::Ok);
  EXPECT(futex_wait(&g_never, 0, kForever) == Status::Interrupted);
  EXPECT(g_count[ap::kSigAlarm] == 2);
  print("signals: futex_wait interrupted\n");

  // 3. Заблокированный сигнал ждёт.
  auto old = sig_mask(1ull << ap::kSigAlarm, 0);
  EXPECT(old.ok() && old.value == 0);
  EXPECT(sig_alarm(now_ns() + 10'000'000) == Status::Ok);
  sleep_until(now_ns() + 60'000'000);
  EXPECT(g_count[ap::kSigAlarm] == 2);
  EXPECT(sig_mask(0, 1ull << ap::kSigAlarm).ok());  // доставка на выходе из вызова
  EXPECT(g_count[ap::kSigAlarm] == 3);
  print("signals: masked alarm delivered on unmask\n");

  // 4. Fault: пропустить инструкцию.
  uint64_t v;
  asm volatile("xor %%eax, %%eax\n\tmov (%%rax), %%rax" : "=a"(v) : : "memory");
  EXPECT(v == 77 && g_count[ap::kSigFault] == 1);
  EXPECT(g_fault_code == 14 && g_fault_addr == 0);
  print("signals: fault handled\n");

  // 5. signal_send ребёнку; Child — нам.
  auto exe = fs_open(root, "bin/spawn_child", 0);
  EXPECT(exe.ok());
  Handle exe_h = static_cast<Handle>(exe.value);
  auto port = port_create();
  EXPECT(port.ok());
  Handle port_h = static_cast<Handle>(port.value);
  auto c = spawn_child(exe_h, kSleep, sizeof kSleep, 0);
  EXPECT(c.ok());
  Handle c_h = static_cast<Handle>(c.value);
  EXPECT(sig_send(c_h, ap::kSigUser1) == Status::Ok);  // игнорируется
  sleep_until(now_ns() + 20'000'000);
  ap::ProcessInfo info;
  EXPECT(process_info(c_h, &info) == Status::Ok && info.state == ap::kProcessRunning);
  EXPECT(sig_send(c_h, 0) == Status::InvalidArgs);
  EXPECT(object_bind(port_h, c_h, 1) == Status::Ok);
  EXPECT(sig_send(c_h, ap::kSigTerminate) == Status::Ok);
  uint64_t code;
  EXPECT(wait_packet(port_h, 1, abi::syscall::kPacketProcessExited, &code));
  EXPECT(static_cast<int64_t>(code) == ap::kExitKilled);
  wait_count(ap::kSigChild, 1);
  EXPECT(g_count[ap::kSigChild] == 1);
  print("signals: terminate and child ok\n");

  // 6. Лимиты Job.
  auto sub = job_create(job);
  EXPECT(sub.ok());
  Handle sub_h = static_cast<Handle>(sub.value);
  ap::JobLimits lim{};
  lim.processes = 1;
  lim.cpu_ns = 100'000'000;  // 100 мс CPU
  EXPECT(job_set_limits(sub_h, &lim) == Status::Ok);
  auto spinner = spawn_child(exe_h, kSpin, sizeof kSpin, sub_h);
  EXPECT(spinner.ok());
  EXPECT(spawn_child(exe_h, kSleep, sizeof kSleep, sub_h).status == Status::NoSpace);
  EXPECT(object_bind(port_h, sub_h, 2) == Status::Ok);
  EXPECT(wait_packet(port_h, 2, abi::syscall::kPacketJobEmpty, &code));  // cpu_ns исчерпан
  EXPECT(process_info(static_cast<Handle>(spinner.value), &info) == Status::Ok);
  EXPECT(info.exit_code == ap::kExitKilled);
  ap::JobInfo ji;
  EXPECT(job_info(sub_h, &ji) == Status::Ok && ji.cpu_ns >= 100'000'000 && ji.processes == 0);
  print("signals: job limits ok\n");

  // 7. Приоритет потока.
  uint64_t tstack = new_stack(16 * 1024);
  auto t = thread_create(idle_thread, tstack, 0);
  EXPECT(t.ok());
  Handle t_h = static_cast<Handle>(t.value);
  EXPECT(thread_set_priority(t_h, 0) == Status::InvalidArgs);
  EXPECT(thread_set_priority(t_h, 32) == Status::InvalidArgs);
  EXPECT(thread_set_priority(t_h, 20) == Status::Ok);
  EXPECT(thread_set_priority(port_h, 20) == Status::WrongType);
  print("signals: ok\n");
  return 0;
}
