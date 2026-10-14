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
                                             int cpu = kAnyCpu);

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
void wake(Thread* t);
// Переключается на следующий готовый поток. Прерывания должны быть запрещены,
// spinlock'и — отпущены.
void schedule();

// Из обработчика прерывания таймера.
void on_timer_tick();

// Статистика для тестов.
uint64_t context_switches(uint32_t cpu);
uint64_t threads_created();

}  // namespace kernel::sched
