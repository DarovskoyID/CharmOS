#include "process.hpp"

#include <abi/process.hpp>
#include <abi/syscall.hpp>
#include <arch/x86_64/cpu.hpp>
#include <lib/elf/elf64.hpp>

#include "../core/panic.hpp"
#include "../fs/vfs.hpp"
#include "../core/placement_new.hpp"
#include "../mm/heap.hpp"
#include "../mm/layout.hpp"
#include "uaccess.hpp"

extern "C" [[noreturn]] void enter_user(uint64_t rip, uint64_t rsp, uint64_t arg);

namespace kernel::proc {

using arch::x86_64::kMapNoExec;
using arch::x86_64::kMapWrite;
using arch::x86_64::kPageSize;
using lib::core::Result;
using lib::core::StringView;

namespace {

constinit uint64_t g_next_pid = 1;

// Все живые процессы (для job_kill и сигналов Job).
constinit sync::Spinlock g_procs_lock{sync::LockRank::ProcList, "proc.list"};
constinit Process* g_procs = nullptr;

void list_add(Process* p) {
  sync::SpinGuard guard(g_procs_lock);
  p->next_proc = g_procs;
  g_procs = p;
}

void list_remove(Process* p) {
  sync::SpinGuard guard(g_procs_lock);
  for (Process** it = &g_procs; *it != nullptr; it = &(*it)->next_proc) {
    if (*it == p) {
      *it = p->next_proc;
      return;
    }
  }
}

// FXSAVE-образ начального состояния (SDM Vol. 2A, FXSAVE, табл. 3-43):
// FCW (смещение 0) = 0x037F, MXCSR (смещение 24) = 0x1F80 — значения после
// FNINIT и по умолчанию psABI §3.2.3. Остальное нулевое (теги x87 — пусто).
uint8_t* new_fpu_image() {
  auto mem = mm::kmalloc(512);
  if (mem.is_err()) return nullptr;
  auto* image = static_cast<uint8_t*>(mem.value());
  for (int i = 0; i < 512; ++i) image[i] = 0;
  image[0] = 0x7f;
  image[1] = 0x03;
  uint32_t mxcsr = 0x1f80;
  __builtin_memcpy(image + 24, &mxcsr, sizeof mxcsr);
  return image;
}

bool load_segments(Process* p, const lib::elf::Image& img, const uint8_t* file) {
  for (uint16_t i = 0; i < img.phnum; ++i) {
    const lib::elf::Elf64Phdr& ph = img.phdrs[i];
    if (ph.p_type != lib::elf::kPtLoad || ph.p_memsz == 0) continue;
    uint64_t flags = 0;
    if ((ph.p_flags & lib::elf::kPfW) != 0) flags |= kMapWrite;
    if ((ph.p_flags & lib::elf::kPfX) == 0) flags |= kMapNoExec;
    uint64_t v0 = ph.p_vaddr & ~(kPageSize - 1);
    uint64_t v1 = (ph.p_vaddr + ph.p_memsz + kPageSize - 1) & ~(kPageSize - 1);
    for (uint64_t v = v0; v < v1; v += kPageSize) {
      uint64_t phys = 0;
      if (p->space.map_new(v, flags, &phys).is_err()) return false;
      // Байты файла, попадающие на эту страницу; остальное уже обнулено.
      uint64_t seg_file_end = ph.p_vaddr + ph.p_filesz;
      uint64_t from = v < ph.p_vaddr ? ph.p_vaddr : v;
      uint64_t to = v + kPageSize < seg_file_end ? v + kPageSize : seg_file_end;
      auto* dst = mm::phys_to_virt<uint8_t>(phys);
      for (uint64_t a = from; a < to; ++a) dst[a - v] = file[ph.p_offset + (a - ph.p_vaddr)];
    }
  }
  return true;
}

// Первое, что делает поток процесса: встаёт в список потоков (для
// прерывания при убийстве) и отмечает свой CPU (для сброса TLB).
void register_current(Process* p) {
  sched::Thread* me = sched::current();
  __atomic_fetch_or(&p->cpus, 1ull << me->cpu, __ATOMIC_ACQ_REL);
  sync::SpinGuard guard(p->threads_lock);
  me->proc_next = p->threads;
  p->threads = me;
}

void process_main(void* arg) {
  auto* p = static_cast<Process*>(arg);
  register_current(p);
  enter_user(p->entry, kUserStackTop, p->bootstrap_handle);
}

struct ThreadStart {
  uint64_t entry, stack_top, arg;
};

void thread_main(void* arg) {
  ThreadStart s = *static_cast<ThreadStart*>(arg);
  mm::kfree(arg);
  register_current(current_process());
  // psABI: RSP = stack_top (как у _start); функции на C нужен переходник libc.
  enter_user(s.entry, s.stack_top, s.arg);
}

// Процесс завершается: флаг и прерывание ожиданий всех потоков. Первый код
// побеждает. Под threads_lock.
void begin_exit_locked(Process* p, int64_t code, bool killed) {
  if (p->exiting) return;
  p->exit_code = code;
  p->killed = killed;
  __atomic_store_n(&p->exiting, true, __ATOMIC_RELEASE);
  for (sched::Thread* t = p->threads; t != nullptr; t = t->proc_next) sched::interrupt(t);
}

// Выход текущего потока; последний разбирает процесс.
[[noreturn]] void leave(int64_t thread_code) {
  Process* p = current_process();
  if (p == nullptr) panic("proc: exit from a kernel thread");
  sched::Thread* me = sched::current();
  // Сначала уйти с CR3 процесса: последний поток (возможно, на другом CPU)
  // разберёт его таблицы.
  arch::x86_64::cli();
  arch::x86_64::write_cr3(mm::kernel_root());
  uint8_t* fpu = me->fpu;
  void* obj = me->user_obj;
  sched::detach_user_context();
  bool last;
  {
    sync::SpinGuard guard(p->threads_lock);
    for (sched::Thread** it = &p->threads; *it != nullptr; it = &(*it)->proc_next) {
      if (*it == me) {
        *it = me->proc_next;
        break;
      }
    }
    last = --p->live_threads == 0;
    if (last && !p->exiting) {
      p->exit_code = thread_code;
      p->exiting = true;  // поздний request_kill — ничего не делает
    }
  }
  mm::kfree(fpu);
  if (obj != nullptr) ipc::thread_exited(obj, thread_code);
  ipc::job_thread_leave(p->job);
  if (last) {
    ipc::teardown_process(p);  // до destroy: страницы VMO не принадлежат процессу
    p->space.destroy();
    ipc::process_exited(p);  // dead, пакеты подписчикам
    list_remove(p);
    ipc::Job* job = p->job;
    p->exited.up();
    put_process(p);  // ссылка «жизни»; дальше p не трогаем
    ipc::job_leave(job);
  }
  sched::exit_thread();
}

}  // namespace

Result<Process*, ProcError> spawn_process(StringView name, const void* elf, size_t size, int cpu,
                                          ipc::Cap* bootstrap, ipc::Job* job) {
  using R = Result<Process*, ProcError>;
  lib::elf::Image img;
  auto err = lib::elf::parse_image(elf, size, kUserImageMin, kUserStackTop - kUserStackSize, &img);
  if (err != lib::elf::Error::None) {
    kprint("proc: {}: bad ELF: {}\n", name, lib::elf::error_name(err));
    return R::err(ProcError::BadImage);
  }

  auto mem = mm::kmalloc(sizeof(Process));
  if (mem.is_err()) return R::err(ProcError::OutOfMemory);
  auto* p = static_cast<Process*>(mem.value());
  // Process содержит Semaphore и Spinlock с конструкторами — заполняем явно.
  __builtin_memset(static_cast<void*>(p), 0, sizeof(Process));
  new (&p->exited) sync::Semaphore("proc.exited", 0);
  new (&p->threads_lock) sync::Spinlock(sync::LockRank::ProcThreads, "proc.threads");
  p->pid = __atomic_fetch_add(&g_next_pid, 1, __ATOMIC_RELAXED);
  size_t n = name.size() < sizeof p->name - 1 ? name.size() : sizeof p->name - 1;
  for (size_t i = 0; i < n; ++i) p->name[i] = name[i];
  p->entry = img.entry;
  p->live_threads = 1;
  p->refs = 2;  // «жизнь» + вызывающий
  p->job = job != nullptr ? job : ipc::root_job();
  if (ipc::job_enter(p->job) != ipc::Status::Ok) {
    mm::kfree(p);
    return R::err(ProcError::NoSpace);
  }

  uint8_t* fpu = new_fpu_image();
  if (fpu == nullptr || ipc::init_process(p) != ipc::Status::Ok || p->space.create().is_err()) {
    if (fpu != nullptr) mm::kfree(fpu);
    if (p->ipc.slots != nullptr) mm::kfree(p->ipc.slots);
    ipc::job_leave(p->job);
    mm::kfree(p);
    return R::err(ProcError::OutOfMemory);
  }

  bool ok = load_segments(p, img, static_cast<const uint8_t*>(elf));
  for (uint64_t v = kUserStackTop - kUserStackSize; ok && v < kUserStackTop; v += kPageSize) {
    ok = p->space.map_new(v, kMapWrite | kMapNoExec, nullptr).is_ok();
  }
  if (ok && bootstrap != nullptr) ok = ipc::install(p, bootstrap, &p->bootstrap_handle) == ipc::Status::Ok;
  sched::UserContext user{p->space.root(), p, fpu, nullptr};
  if (ok) list_add(p);
  auto thread = ok ? sched::spawn(p->name, &process_main, p, cpu, &user)
                   : lib::core::Result<sched::Thread*, sched::SchedError>::err(
                         sched::SchedError::OutOfMemory);
  if (thread.is_err()) {
    // Стартовую капабилити (если её уже поставили в таблицу) не уничтожаем:
    // она возвращается вызывающему отсоединённой.
    if (p->bootstrap_handle != 0) (void)p->ipc.table.remove(p->bootstrap_handle);
    if (ok) list_remove(p);
    ipc::teardown_process(p);
    p->space.destroy();
    ipc::job_leave(p->job);
    mm::kfree(fpu);
    mm::kfree(p);
    return R::err(ProcError::OutOfMemory);
  }
  return R::ok(p);
}

int64_t wait_process(Process* p, void (*before_free)(void*), void* ctx) {
  p->exited.down();
  if (before_free != nullptr) before_free(ctx);
  int64_t code = p->exit_code;
  put_process(p);
  return code;
}

void get_process_ref(Process* p) { __atomic_fetch_add(&p->refs, 1, __ATOMIC_RELAXED); }

void put_process(Process* p) {
  if (__atomic_sub_fetch(&p->refs, 1, __ATOMIC_ACQ_REL) == 0) mm::kfree(p);
}

Process* current_process() { return static_cast<Process*>(sched::current()->process); }

void exit_current(int64_t code) {
  Process* p = current_process();
  if (p == nullptr) panic("proc: exit from a kernel thread");
  {
    sync::SpinGuard guard(p->threads_lock);
    begin_exit_locked(p, code, false);
  }
  leave(code);
}

void exit_current_thread(int64_t code) { leave(code); }

void request_kill(Process* p) {
  sync::SpinGuard guard(p->threads_lock);
  begin_exit_locked(p, kExitKilled, true);
}

void check_kill() {
  Process* p = current_process();
  if (p == nullptr || !__atomic_load_n(&p->exiting, __ATOMIC_ACQUIRE)) return;
  bool report;
  {
    sync::SpinGuard guard(p->threads_lock);
    report = p->killed && !p->kill_reported;
    p->kill_reported = true;
  }
  if (report) kprint("proc: {} (pid {}) killed by kernel request\n", p->name, p->pid);
  leave(p->exit_code);
}

void kill_current(const arch::x86_64::InterruptFrame& f) {
  Process* p = current_process();
  if (p == nullptr) panic("proc: user-mode exception without a process");
  if (f.vector == arch::x86_64::kVectorPageFault) {
    kprint("proc: {} (pid {}) killed: {} at rip={X} addr={X} error={x}\n", p->name, p->pid,
           arch::x86_64::exception_name(f.vector), f.rip, arch::x86_64::read_cr2(), f.error_code);
  } else {
    kprint("proc: {} (pid {}) killed: {} at rip={X} error={x}\n", p->name, p->pid,
           arch::x86_64::exception_name(f.vector), f.rip, f.error_code);
  }
  {
    sync::SpinGuard guard(p->threads_lock);
    begin_exit_locked(p, kExitKilled, true);
    p->kill_reported = true;  // уже напечатано
  }
  leave(kExitKilled);
}

// --- потоки (process.md §4) -------------------------------------------------------------

Status sys_thread_create(uint64_t entry, uint64_t stack_top, uint64_t arg, uint64_t* out) {
  *out = 0;
  if (entry < kUserImageMin || entry >= abi::syscall::kUserTop || stack_top <= kUserImageMin ||
      stack_top > abi::syscall::kUserTop) {
    return Status::BadAddress;
  }
  if (stack_top % 16 != 0) return Status::InvalidArgs;
  Process* p = current_process();
  auto start_mem = mm::kmalloc(sizeof(ThreadStart));
  uint8_t* fpu = new_fpu_image();
  void* obj = nullptr;
  uint64_t handle = 0;
  Status s = start_mem.is_ok() && fpu != nullptr ? ipc::create_thread_obj(&obj, &handle) : Status::NoMemory;
  if (s != Status::Ok) {
    if (start_mem.is_ok()) mm::kfree(start_mem.value());
    if (fpu != nullptr) mm::kfree(fpu);
    return s;
  }
  auto* start = static_cast<ThreadStart*>(start_mem.value());
  *start = {entry, stack_top, arg};
  if (ipc::job_thread_enter(p->job) != Status::Ok) {
    mm::kfree(start);
    mm::kfree(fpu);
    (void)ipc::sys_handle_close(static_cast<uint32_t>(handle));
    ipc::thread_exited(obj, 0);
    return Status::NoSpace;  // лимит потоков Job (§7)
  }
  bool dying;
  {
    sync::SpinGuard guard(p->threads_lock);
    dying = p->exiting;
    if (!dying) ++p->live_threads;
  }
  sched::UserContext user{p->space.root(), p, fpu, obj};
  auto t = dying ? lib::core::Result<sched::Thread*, sched::SchedError>::err(sched::SchedError::OutOfMemory)
                 : sched::spawn(p->name, &thread_main, start, sched::kAnyCpu, &user);
  if (t.is_err()) {
    if (!dying) {
      sync::SpinGuard guard(p->threads_lock);
      --p->live_threads;  // не последний: вызывающий поток жив
    }
    mm::kfree(start);
    mm::kfree(fpu);
    (void)ipc::sys_handle_close(static_cast<uint32_t>(handle));
    ipc::thread_exited(obj, 0);  // ссылка потока
    ipc::job_thread_leave(p->job);
    return dying ? Status::Interrupted : Status::NoMemory;
  }
  *out = handle;
  return Status::Ok;
}

Status sys_thread_set_fs(uint64_t base) {
  if (base >= abi::syscall::kUserTop) return Status::InvalidArgs;  // неканонический или ядро
  bool irq = (arch::x86_64::read_rflags() & arch::x86_64::kRflagsIf) != 0;
  arch::x86_64::cli();
  sched::current()->fs_base = base;
  arch::x86_64::set_fs_base(base);
  if (irq) arch::x86_64::sti();
  return Status::Ok;
}

// --- запуск процессов (process.md §2, §3) -------------------------------------------------

namespace {

namespace ap = abi::process;
constexpr uint64_t kMaxExecutable = 64ull << 20;

// Блок строк: ровно count строк, каждая с завершающим \0.
bool valid_strings(const char* b, uint32_t bytes, uint32_t count) {
  if (bytes == 0) return count == 0;
  if (b[bytes - 1] != '\0') return false;
  uint32_t n = 0;
  for (uint32_t i = 0; i < bytes; ++i) n += b[i] == '\0' ? 1 : 0;
  return n == count;
}

Status map_proc_error(ProcError e) {
  switch (e) {
    case ProcError::BadImage:
      return Status::InvalidArgs;
    case ProcError::NoSpace:
      return Status::NoSpace;
    case ProcError::OutOfMemory:
      break;
  }
  return Status::NoMemory;
}

}  // namespace

Status sys_process_spawn(uint64_t uargs, uint64_t* out) {
  *out = 0;
  ap::SpawnArgs a;
  if (!copy_from_user(&a, uargs, sizeof a)) return Status::BadAddress;
  if (a.handle_count > ap::kMaxStartHandles || a.argc > ap::kMaxArgs || a.args_bytes > ap::kMaxArgsBytes ||
      a.envc > ap::kMaxEnv || a.env_bytes > ap::kMaxEnvBytes || a.name_len > ap::kMaxName || a.flags != 0 ||
      a.reserved[0] != 0 || a.reserved[1] != 0 || a.priority > ap::kPriorityMax) {
    return Status::InvalidArgs;
  }
  uint32_t handles[ap::kMaxStartHandles];
  ap::StartHeader hdr{};
  char name[ap::kMaxName + 1] = {};
  if (!copy_from_user(handles, reinterpret_cast<uint64_t>(a.handles), a.handle_count * sizeof(uint32_t)) ||
      !copy_from_user(hdr.roles, reinterpret_cast<uint64_t>(a.roles), a.handle_count * sizeof(uint32_t)) ||
      !copy_from_user(name, reinterpret_cast<uint64_t>(a.name), a.name_len)) {
    return Status::BadAddress;
  }
  if (a.name_len == 0) __builtin_memcpy(name, "proc", 5);

  // Стартовое сообщение: заголовок, аргументы, окружение (§2).
  uint64_t len = sizeof hdr + a.args_bytes + a.env_bytes;
  auto mem = mm::kmalloc(static_cast<size_t>(len));
  if (mem.is_err()) return Status::NoMemory;
  auto* msg = static_cast<char*>(mem.value());
  __builtin_memcpy(hdr.magic, ap::kStartMagic, 4);
  hdr.argc = a.argc;
  hdr.args_bytes = a.args_bytes;
  hdr.handle_count = a.handle_count;
  hdr.envc = a.envc;
  hdr.env_bytes = a.env_bytes;
  __builtin_memcpy(msg, &hdr, sizeof hdr);
  char* args = msg + sizeof hdr;
  char* env = args + a.args_bytes;
  Status s = Status::Ok;
  if (!copy_from_user(args, reinterpret_cast<uint64_t>(a.args), a.args_bytes) ||
      !copy_from_user(env, reinterpret_cast<uint64_t>(a.env), a.env_bytes)) {
    s = Status::BadAddress;
  } else if (!valid_strings(args, a.args_bytes, a.argc) || !valid_strings(env, a.env_bytes, a.envc)) {
    s = Status::InvalidArgs;
  }

  ipc::Job* job = nullptr;
  if (s == Status::Ok) {
    if (a.job != 0) {
      s = ipc::get_job(a.job, abi::syscall::rights::kSpawn, &job);
    } else {
      job = current_process()->job;
      ipc::job_get(job);
    }
  }
  void* elf = nullptr;
  uint64_t elf_size = 0;
  if (s == Status::Ok) s = fs::read_file(a.executable, kMaxExecutable, &elf, &elf_size);
  ipc::Cap *mine = nullptr, *theirs = nullptr;
  if (s == Status::Ok) s = ipc::create_channel(&mine, &theirs);
  Process* child = nullptr;
  if (s == Status::Ok) {
    auto r = spawn_process(StringView(name, a.name_len != 0 ? a.name_len : 4), elf, static_cast<size_t>(elf_size),
                           sched::kAnyCpu, theirs, job);
    if (r.is_err()) {
      s = map_proc_error(r.error());
      ipc::release_detached(theirs);
    } else {
      child = r.value();
    }
  }
  if (elf != nullptr) mm::kfree(elf);
  if (job != nullptr) ipc::job_put(job);  // у ребёнка своя ссылка
  // Ребёнок уже запущен и ждёт сообщения; хендлы перемещаются атомарно.
  if (s == Status::Ok) {
    s = ipc::send_from_table(mine, msg, len, handles, a.handle_count);
    if (s != Status::Ok) request_kill(child);
  }
  if (mine != nullptr) ipc::release_detached(mine);
  mm::kfree(msg);
  if (s == Status::Ok) {
    uint64_t h;
    s = ipc::install_process(child, &h);
    if (s == Status::Ok) {
      *out = h;
      return Status::Ok;
    }
    request_kill(child);
  }
  if (child != nullptr) put_process(child);
  return s;
}

Status sys_process_kill(uint32_t h) {
  Process* p;
  Status s = ipc::get_process(h, abi::syscall::rights::kWrite, &p);
  if (s != Status::Ok) return s;
  {
    sync::SpinGuard guard(p->threads_lock);
    begin_exit_locked(p, kExitKilled, false);  // §3.2: код -1000, не сигнал
  }
  put_process(p);
  return Status::Ok;
}

Status sys_process_info(uint32_t h, uint64_t user_out) {
  Process* p;
  Status s = ipc::get_process(h, abi::syscall::rights::kRead, &p);
  if (s != Status::Ok) return s;
  ap::ProcessInfo info{};
  info.pid = p->pid;
  bool dead = __atomic_load_n(&p->dead, __ATOMIC_ACQUIRE);
  info.state = dead ? ap::kProcessExited : ap::kProcessRunning;
  info.exit_code = dead ? p->exit_code : 0;
  put_process(p);
  return copy_to_user(user_out, &info, sizeof info) ? Status::Ok : Status::BadAddress;
}

namespace {

// Завершает процессы поддерева Job. Под g_procs_lock (можно из прерывания).
void terminate_job_locked(const ipc::Job* j, int64_t code) {
  for (Process* p = g_procs; p != nullptr; p = p->next_proc) {
    if (!ipc::job_contains(j, p->job)) continue;
    sync::SpinGuard guard(p->threads_lock);
    begin_exit_locked(p, code, false);
  }
}

}  // namespace

Status sys_job_kill(uint32_t h) {
  ipc::Job* j;
  Status s = ipc::get_job(h, abi::syscall::rights::kWrite, &j);
  if (s != Status::Ok) return s;
  {
    sync::SpinGuard guard(g_procs_lock);
    terminate_job_locked(j, kExitKilled);
  }
  ipc::job_put(j);
  return Status::Ok;
}

Status sys_console_set_foreground(uint32_t con, uint32_t job) {
  Status s = ipc::get_console(con, abi::syscall::rights::kWrite);
  if (s != Status::Ok) return s;
  ipc::Job* j;
  s = ipc::get_job(job, abi::syscall::rights::kWrite, &j);
  if (s != Status::Ok) return s;
  s = ipc::set_foreground(j);
  ipc::job_put(j);
  return s;
}

void signal_foreground(uint32_t signo) {
  // Сигналы с обработчиками — §9; без обработчика Interrupt и Quit
  // завершают процесс (код -1000, как убийство ядром).
  (void)signo;
  ipc::with_foreground(
      [](ipc::Job* j, void*) {
        sync::SpinGuard guard(g_procs_lock);
        terminate_job_locked(j, kExitKilled);
      },
      nullptr);
}

}  // namespace kernel::proc
