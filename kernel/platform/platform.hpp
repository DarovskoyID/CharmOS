#pragma once
// Платформа PC: ACPI → контроллеры прерываний → источники времени.
// Порядок в platform_init: таблицы ACPI, маскирование 8259, Local APIC,
// все I/O APIC (все входы замаскированы), HPET, калибровка TSC и таймера APIC.

#include <abi/boot.hpp>
#include <arch/x86_64/apic.hpp>
#include <arch/x86_64/hpet.hpp>
#include <lib/acpi/parse.hpp>

namespace kernel::platform {

// Векторы, занятые платформой.
inline constexpr uint8_t kVectorTimer = 0x20;
inline constexpr uint8_t kVectorTest = 0x40;  // ktest маршрутизации через I/O APIC
inline constexpr uint8_t kVectorApicError = 0xfe;
inline constexpr uint8_t kVectorSpurious = 0xff;

void platform_init(const abi::boot::BootInfo& info);

const lib::acpi::MadtInfo& madt();
const lib::acpi::FadtInfo& fadt();
arch::x86_64::LocalApic& local_apic();
const arch::x86_64::Hpet& hpet();

// I/O APIC, обслуживающий данный GSI, либо nullptr.
const arch::x86_64::IoApic* io_apic_for(uint32_t gsi);

uint64_t tsc_hz();
uint64_t apic_timer_hz();  // частота счёта таймера APIC при делителе 16

// Периодический тик таймера APIC с частотой hz; включает прерывания.
void start_tick(uint32_t hz);
uint64_t ticks();

}  // namespace kernel::platform
