#pragma once
// Процессы: адресное пространство и потоки в ring 3 (docs/abi/process.md §4:
// с v1.5 потоков может быть несколько). Создаются из ELF-образа (обычно из
// initrd). Процесс завершается с выходом последнего потока.

#include <arch/x86_64/idt.hpp>
#include <lib/core/result.hpp>
#include <lib/core/string_view.hpp>

#include "../ipc/ipc.hpp"
#include "../mm/address_space.hpp"
#include "../sched/sched.hpp"
#include "../sync/spinlock.hpp"
#include "../sync/wait.hpp"

namespace kernel::proc {

using abi::syscall::Status;

// Код завершения процесса, убитого ядром (исключение в ring 3).
inline constexpr int64_t kExitKilled = -1000;

// Раскладка нижней половины.
inline constexpr uint64_t kUserImageMin = 0x10000;        // ниже — не отображается
inline constexpr uint64_t kUserStackTop = 0x00007ffffff00000ull;
inline constexpr uint64_t kUserStackSize = 64 * 1024;     // под ним — защитная дыра

enum class ProcError : uint8_t { OutOfMemory, BadImage, NoSpace };

struct Process {
  uint64_t pid;
  char name[32];
  mm::AddressSpace space;
  uint64_t entry;
  int64_t exit_code;  // при exiting — код процесса
  sync::Semaphore exited{"proc.exited", 0};
  ipc::ProcessIpc ipc;
  // CPU, на которых были потоки процесса (потоки не мигрируют): кому
  // сбрасывать TLB при снятии отображений. Только растёт.
  uint64_t cpus;
  uint32_t bootstrap_handle;  // RDI при старте (docs/abi/syscalls.md §7), 0 — нет

  // Под threads_lock.
  sync::Spinlock threads_lock{sync::LockRank::ProcThreads, "proc.threads"};
  sched::Thread* threads;  // запущенные потоки
  uint32_t live_threads;   // созданные и ещё не завершившиеся
  bool exiting;            // процесс завершается: каждый поток выходит при возврате в ring 3
  bool killed;             // ... по решению ядра (request_kill, исключение)
  bool kill_reported;

  // Ссылки (атомарно): «жизнь» (до завершения), вызывающий spawn_process,
  // хендлы Process. Последняя освобождает структуру.
  uint32_t refs;
  ipc::Job* job;     // ссылка; не меняется
  void* watchers;    // объекты Process с подписками (под блокировкой IPC)
  bool dead;         // завершён: exit_code окончателен
  Process* next_proc;  // общий список процессов
  Process* parent;     // запустивший процесс (ссылка) — ему сигнал Child
  uint64_t charged_pages;  // образ и стек, учтённые в Job (§7)

  // Сигналы (process.md §9).
  uint64_t sig_entry;  // 0 — обработчика нет
  uint64_t sig_stack_top;
  uint64_t sig_stack_size;
  uint64_t sig_pending;  // бит n — сигнал n ждёт доставки
  uint64_t alarm_ns;     // signal_alarm, 0 — нет
};

// bootstrap — отсоединённая капабилити стартового канала (или nullptr);
// при успехе переходит процессу, при ошибке остаётся у вызывающего.
lib::core::Result<Process*, ProcError> spawn_process(lib::core::StringView name,
                                                     const void* elf, size_t size,
                                                     int cpu = sched::kAnyCpu,
                                                     ipc::Cap* bootstrap = nullptr,
                                                     ipc::Job* job = nullptr,
                                                     Process* parent = nullptr,
                                                     uint32_t priority = 0);

void get_process_ref(Process* p);
void put_process(Process* p);

// Ждёт завершения, снимает ссылку вызывающего spawn_process и возвращает код.
// before_free(ctx) вызывается после завершения, но до освобождения.
int64_t wait_process(Process* p, void (*before_free)(void*) = nullptr, void* ctx = nullptr);

// Просит убить процесс (код kExitKilled). Можно из прерывания. Потоки
// завершатся при ближайшем выходе из системного вызова или по тику таймера в
// ring 3; прерываемые ожидания (IPC, futex, сон) прерываются сразу.
void request_kill(Process* p);
// Из обработчиков, вернувшихся бы в ring 3: завершает текущий поток, если
// процесс завершается.
void check_kill();

Process* current_process();

// process_exit: весь процесс с кодом code.
[[noreturn]] void exit_current(int64_t code);
// thread_exit: только вызвавший поток; последний — весь процесс.
[[noreturn]] void exit_current_thread(int64_t code);
// Из обработчика исключения, пришедшего из ring 3.
[[noreturn]] void kill_current(const arch::x86_64::InterruptFrame& frame);

// Процессы и Job (process.md §3, §5).
Status sys_process_spawn(uint64_t args, uint64_t* out);
Status sys_process_kill(uint32_t h);
Status sys_process_info(uint32_t h, uint64_t out);
Status sys_job_kill(uint32_t h);
Status sys_job_set_limits(uint32_t h, uint64_t limits);
Status sys_thread_set_priority(uint32_t thread, uint64_t priority);
// Учёт времени CPU (хук планировщика) и лимит cpu_ns.
void init_accounting();
Status sys_console_set_foreground(uint32_t con, uint32_t job);
// Управляющий байт консоли (Ctrl-C → 1, Ctrl-\ → 2): сигнал переднему Job.
// Из прерывания.
void signal_foreground(uint32_t signo);

// Системные вызовы потоков (process.md §4).
Status sys_thread_create(uint64_t entry, uint64_t stack_top, uint64_t arg, uint64_t* out);
Status sys_thread_set_fs(uint64_t base);
Status sys_futex_wait(uint64_t addr, uint64_t expected, uint64_t deadline);
Status sys_futex_wake(uint64_t addr, uint64_t count, uint64_t* woken);

}  // namespace kernel::proc
