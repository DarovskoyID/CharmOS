#pragma once
// HPET (docs/specs/hpet-1.0a.pdf §2.3). Главный счётчик — эталон времени
// для калибровки TSC и таймера Local APIC; компараторы умеют давать прерывания
// через I/O APIC.

#include <stdint.h>

namespace arch::x86_64 {

class Hpet {
 public:
  // Включает главный счётчик (без legacy-маршрутизации). false — регистры
  // выглядят неправдоподобно (период 0 или больше 100 нс).
  bool init(uint64_t mmio_base);

  uint64_t counter() const;
  uint64_t period_fs() const { return period_fs_; }
  uint64_t frequency_hz() const { return 1'000'000'000'000'000ull / period_fs_; }
  bool counter_is_64bit() const { return wide_; }
  uint32_t timer_count() const { return timers_; }

  // Тики счётчика между двумя отсчётами с учётом переполнения 32-битного счётчика.
  uint64_t elapsed(uint64_t from, uint64_t to) const;
  uint64_t ns_to_ticks(uint64_t ns) const;
  uint64_t ticks_to_ns(uint64_t ticks) const;
  // Активное ожидание.
  void wait_ns(uint64_t ns) const;

  // Компаратор n: маска допустимых входов I/O APIC (Tn_INT_ROUTE_CAP).
  uint32_t route_capability(uint32_t n) const;
  // Однократное прерывание по фронту через вход I/O APIC gsi в момент counter() + ticks.
  void arm_one_shot(uint32_t n, uint32_t gsi, uint64_t ticks) const;
  void disarm(uint32_t n) const;

 private:
  uint64_t base_ = 0;
  uint64_t period_fs_ = 0;
  uint32_t timers_ = 0;
  bool wide_ = false;
};

}  // namespace arch::x86_64
