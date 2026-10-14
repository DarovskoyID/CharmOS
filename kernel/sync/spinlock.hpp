#pragma once
// Spinlock ядра: тикетный (честная очередь), на время удержания прерывания на
// этом CPU запрещены — обработчик прерывания не может взять ту же блокировку
// и зависнуть на собственном процессоре.
//
// ПРАВИЛО ПОРЯДКА ЗАХВАТА. У каждой блокировки есть ранг (LockRank). Держа
// блокировки, можно брать только блокировку со строго большим рангом, чем у
// любой удерживаемой. Нарушение — немедленная паника с именами обеих
// блокировок: дедлок из-за порядка обнаруживается при первом же неверном
// захвате, а не когда два CPU случайно встретятся. Новый ранг — сюда, с
// обоснованием места в порядке.

#include <stdint.h>

namespace kernel::sync {

enum class LockRank : uint8_t {
  // Внешние подсистемы: берут блокировки памяти ниже по списку.
  Test = 5,       // ktest
  DevMgr = 6,     // текущий процесс-драйвер привязки (ошибки IOMMU → флаг убийства)
  Ipc = 7,        // все объекты IPC и хендлы → куча, vmm, pmm, очереди планировщика
  Iommu = 8,      // таблицы и регистры IOMMU (берётся под Ipc) → куча, pmm
  Vectors = 9,    // распределение векторов устройств (берётся под Ipc)
  Heap = 10,      // куча → vmm → pmm
  MmStacks = 11,  // стеки ядра → vmm → pmm
  Vmm = 20,       // таблицы страниц ядра → pmm
  Pmm = 30,       // битовая карта фреймов (лист)
  Block = 40,     // реестр дисков блочного слоя (лист)
  Wait = 45,         // внутренняя блокировка Mutex/Semaphore → очередь планировщика
  SchedZombie = 48,  // список завершённых потоков
  SchedQueue = 50,   // очередь готовых потоков CPU (берётся и из прерывания таймера)
  Log = 90,       // журнал: можно печатать, держа что угодно
};

class Spinlock {
 public:
  constexpr Spinlock(LockRank rank, const char* name) : rank_(rank), name_(name) {}
  Spinlock(const Spinlock&) = delete;
  Spinlock& operator=(const Spinlock&) = delete;

  // Возвращает предыдущее состояние флага прерываний (для unlock).
  bool lock();
  void unlock(bool interrupts_were_enabled);
  // Без ожидания; при неудаче ничего не меняет.
  bool try_lock(bool* interrupts_were_enabled);

  LockRank rank() const { return rank_; }
  const char* name() const { return name_; }

 private:
  volatile uint32_t next_ = 0;
  volatile uint32_t serving_ = 0;
  LockRank rank_;
  const char* name_;
};

class SpinGuard {
 public:
  explicit SpinGuard(Spinlock& lock) : lock_(lock), irq_(lock.lock()) {}
  ~SpinGuard() { lock_.unlock(irq_); }
  SpinGuard(const SpinGuard&) = delete;
  SpinGuard& operator=(const SpinGuard&) = delete;

 private:
  Spinlock& lock_;
  bool irq_;
};

}  // namespace kernel::sync
