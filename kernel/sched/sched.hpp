#pragma once
// Потоки ядра и планировщик.
//
// Модель: у каждого CPU своя очередь готовых потоков (FIFO, круговая
// диспетчеризация) и свой поток простоя. Новый поток попадает на наименее
// загруженный CPU (или на заданный) и дальше с него не мигрирует. Вытеснение —
// по тику таймера Local APIC каждого CPU. Блокирующие примитивы
// (kernel/sync/wait.hpp) построены на block_current/wake.
//
// Блокировки: у каждой очереди свой spinlock ранга SchedQueue. Состояние
// потока меняется только под блокировкой очереди его CPU.

#include <stdint.h>

#include <lib/core/result.hpp>

#include "../mm/stack.hpp"

namespace kernel::sched {

enum class State : uint8_t { Ready, Running, Blocked, Sleeping, Dead };

enum class SchedError : uint8_t { OutOfMemory, BadCpu };

struct Thread {
  uint64_t rsp;  // сохранённый указатель стека (switch_context)
  uint64_t id;
  const char* name;
  State state;
  uint32_t cpu;
  volatile bool on_cpu;  // ещё исполняется (стек занят), даже если уже Dead
  bool is_idle;
  mm::KernelStack stack;
  void (*entry)(void*);
  void* arg;
  Thread* next;        // очередь готовых / ожидания / зомби
  uint64_t wake_ns;    // для Sleeping
  Thread* sleep_next;

  // Поток процесса (ring 3). У потоков ядра всё нулевое.
  uint64_t user_cr3;   // PML4 процесса
  void* process;       // proc::Process
  uint8_t* fpu;        // 512 байт FXSAVE, выровнено на 16 (владеет процесс)
  uint64_t fs_base;    // TLS (process.md §4), грузится при переключении
  void* user_obj;      // объект IPC Thread (nullptr у главного потока)
  Thread* proc_next;   // список потоков процесса (под его блокировкой)

  // Прерывание ожиданий (убийство процесса, сигналы): см. interrupt().
  volatile bool interrupt_pending;
  volatile bool interruptible;
};

// Пользовательский контекст для spawn: задаётся до того, как поток попадёт
// в очередь, — первый же запуск идёт с CR3 процесса.
struct UserContext {
  uint64_t cr3;
  void* process;
  uint8_t* fpu;
  void* user_obj;
};

using ThreadFn = void (*)(void* arg);

// Частота тика планировщика на каждом CPU. 100 Гц: под QEMU без KVM (CI)
// более частые тики теряются, а квант 10 мс для ядра без GUI достаточен.
inline constexpr uint32_t kTickHz = 100;
inline constexpr int kAnyCpu = -1;

// Превращает текущий контекст в поток простоя этого CPU и запускает тик
// таймера. Вызывать один раз на каждом CPU после smp::*init*.
void init_this_cpu(uint32_t tick_hz);

// Отдаёт CPU планировщику навсегда (контекст становится простоем).
[[noreturn]] void idle_loop();

lib::core::Result<Thread*, SchedError> spawn(const char* name, ThreadFn fn, void* arg,
                                             int cpu = kAnyCpu,
                                             const UserContext* user = nullptr);

// Поток перестаёт быть потоком процесса (перед завершением процесса):
// планировщик больше не трогает его CR3 и FPU.
void detach_user_context();

Thread* current();
void yield();
void sleep_ns(uint64_t ns);
[[noreturn]] void exit_thread();

// Для примитивов синхронизации. Вызывающий держит свою блокировку и
// запрещённые прерывания: block_current помечает поток Blocked, после чего
// вызывающий отпускает свою блокировку (не включая прерывания) и зовёт
// schedule(). wake может прийти в любой момент после block_current — поток
// не потеряется.
void block_current();
// То же со сроком (абсолютное время now_ns): по истечении поток просыпается
// сам. Бесконечный срок (UINT64_MAX) — обычное block_current.
void block_current_until(uint64_t deadline_ns);
void wake(Thread* t);

// Прерываемые ожидания. interrupt(t) ставит потоку флаг и, если он в
// прерываемом ожидании, будит его; ожидающий видит interrupted() и выходит
// (Interrupted). block_interruptible_until — как block_current_until; после
// schedule() вызывающий зовёт end_interruptible(). Потоки ядра не прерываются.
void interrupt(Thread* t);
bool interrupted();
void block_interruptible_until(uint64_t deadline_ns);
void end_interruptible();
// Переключается на следующий готовый поток. Прерывания должны быть запрещены,
// spinlock'и — отпущены.
void schedule();

// Из обработчика прерывания таймера.
void on_timer_tick();

// Статистика для тестов.
uint64_t context_switches(uint32_t cpu);
uint64_t threads_created();

}  // namespace kernel::sched
