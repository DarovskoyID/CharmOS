#pragma once
// Local APIC (xAPIC, MMIO), I/O APIC и маскирование legacy 8259.
// Local APIC: docs/specs/intel-sdm-vol3a.pdf гл. 11 (табл. 11-1, рис. 11-8, 11-10, 11-23).
// I/O APIC: docs/specs/ioapic-82093aa.pdf §3.
// 8259: docs/specs/8259a.pdf (OCW1). Адреса портов 0x21/0xA1 — раскладка PC/AT,
// в даташите микросхемы их нет.

#include <stdint.h>

namespace arch::x86_64 {

// Маскирует все входы обоих 8259 (ACPI §5.2.12, флаг PCAT_COMPAT: при работе
// через APIC векторы 8259 должны быть замаскированы).
void mask_legacy_pic();

// Один объект обслуживает все процессоры: регистры Local APIC у каждого CPU
// свои, но адрес (MMIO или MSR) одинаковый.
class LocalApic {
 public:
  enum class Mode : uint8_t { XApic, X2Apic };

  // Выбор режима до init. x2APIC — если процессор его поддерживает
  // (CPUID.01H:ECX[21], SDM §11.12.1) и prefer_x2apic; иначе xAPIC через MMIO
  // (mmio_base — виртуальный адрес отображённой страницы регистров).
  void configure(Mode mode, uint64_t mmio_base);
  Mode mode() const { return mode_; }

  // Включает Local APIC текущего процессора в выбранном режиме и настраивает
  // SVR, LVT и TPR. Вызывать на каждом CPU.
  void init_this_cpu(uint8_t spurious_vector, uint8_t error_vector) const;

  uint32_t id() const;
  uint32_t version() const;
  void eoi() const;

  // Межпроцессорные прерывания (SDM §11.6, табл. 9-1).
  void send_init(uint32_t apic_id) const;
  void send_startup(uint32_t apic_id, uint8_t vector_page) const;
  void send_fixed(uint32_t apic_id, uint8_t vector) const;
  void send_fixed_all_but_self(uint8_t vector) const;

  // Таймер (SDM §11.5.4). divide — значение регистра делителя (0xb = 1, 0x3 = 16).
  void timer_one_shot(uint8_t vector, uint32_t divide, uint32_t initial_count) const;
  void timer_periodic(uint8_t vector, uint32_t divide, uint32_t initial_count) const;
  void timer_stop() const;
  uint32_t timer_current() const;

  // Настройка LINT0/LINT1: маскировать или NMI.
  void mask_lint(int lint) const;
  void set_lint_nmi(int lint) const;

 private:
  // reg — смещение регистра в xAPIC (табл. 11-1); в x2APIC оно же даёт
  // номер MSR 0x800 + reg/16 (табл. 11-6).
  uint32_t read(uint32_t reg) const;
  void write(uint32_t reg, uint32_t v) const;
  void send_ipi(uint32_t apic_id, uint32_t low) const;

  Mode mode_ = Mode::XApic;
  uint64_t base_ = 0;
};

// CPUID.01H:ECX[21] — поддержка x2APIC.
bool cpu_has_x2apic();

inline constexpr uint32_t kApicTimerDivide1 = 0xb;
inline constexpr uint32_t kApicTimerDivide16 = 0x3;

// IA32_APIC_BASE (SDM рис. 11-5).
inline constexpr uint32_t kMsrApicBase = 0x1b;
inline constexpr uint64_t kApicBaseEnable = 1ull << 11;
inline constexpr uint64_t kApicBaseX2Apic = 1ull << 10;
inline constexpr uint64_t kApicBaseBsp = 1ull << 8;

class IoApic {
 public:
  void init(uint64_t mmio_base, uint32_t gsi_base);

  uint32_t gsi_base() const { return gsi_base_; }
  uint32_t gsi_count() const { return count_; }
  bool owns(uint32_t gsi) const { return gsi >= gsi_base_ && gsi < gsi_base_ + count_; }
  uint32_t version() const;

  struct Route {
    uint8_t vector;
    bool level_triggered;
    bool active_low;
    uint8_t dest_apic_id;  // физический режим
  };
  void route(uint32_t gsi, const Route& r) const;
  void mask(uint32_t gsi) const;

 private:
  uint32_t read(uint8_t reg) const;
  void write(uint8_t reg, uint32_t v) const;

  uint64_t base_ = 0;
  uint32_t gsi_base_ = 0;
  uint32_t count_ = 0;
};

}  // namespace arch::x86_64
