#pragma once
// ACPI PM-таймер (docs/specs/acpi-6.5.pdf §4.8.3.3): 24- или 32-битный
// счётчик 3.579545 МГц в порту ввода-вывода или в памяти (GAS, §5.2.3.2).
// Эталон времени, когда HPET нет (например, VMware).

#include <stdint.h>

namespace arch::x86_64 {

class PmTimer {
 public:
  static constexpr uint64_t kHz = 3'579'545;

  // mmio — виртуальный адрес (или 0, тогда port). bits — 24 или 32 (TMR_VAL_EXT).
  void init(uint16_t port, uint64_t mmio, uint32_t bits);
  bool present() const { return port_ != 0 || mmio_ != 0; }
  uint64_t counter() const;
  uint64_t elapsed(uint64_t from, uint64_t to) const { return (to - from) & mask_; }
  uint64_t ns_to_ticks(uint64_t ns) const;
  uint64_t ticks_to_ns(uint64_t ticks) const;
  void wait_ns(uint64_t ns) const;

 private:
  uint16_t port_ = 0;
  uint64_t mmio_ = 0;
  uint64_t mask_ = 0xffffff;
};

}  // namespace arch::x86_64
