#pragma once
// Платформа PC: ACPI → контроллеры прерываний → источники времени.
// Порядок в platform_init: таблицы ACPI, маскирование 8259, Local APIC,
// все I/O APIC (все входы замаскированы), HPET, калибровка TSC и таймера APIC.

#include <abi/boot.hpp>
#include <arch/x86_64/apic.hpp>
#include <arch/x86_64/hpet.hpp>
#include <arch/x86_64/idt.hpp>
#include <lib/acpi/parse.hpp>

namespace kernel {
class Cmdline;
}

namespace kernel::platform {

// Векторы, занятые платформой.
inline constexpr uint8_t kVectorTimer = 0x20;
inline constexpr uint8_t kVectorTest = 0x40;  // ktest маршрутизации через I/O APIC
inline constexpr uint8_t kVectorWakeup = 0xf0;    // IPI: разбудить CPU (планировщик)
inline constexpr uint8_t kVectorTlbFlush = 0xf1;  // IPI: сбросить TLB (smp::flush_tlb)
inline constexpr uint8_t kVectorApicError = 0xfe;
inline constexpr uint8_t kVectorSpurious = 0xff;

// cmdline: lapic=xapic — не включать x2APIC (для проверки пути MMIO).
void platform_init(const abi::boot::BootInfo& info, const Cmdline& cmdline);

// Local APIC текущего CPU (BSP — внутри platform_init, AP — при запуске).
void init_this_cpu();

const lib::acpi::MadtInfo& madt();
const lib::acpi::FadtInfo& fadt();
arch::x86_64::LocalApic& local_apic();
const arch::x86_64::Hpet& hpet();

// I/O APIC, обслуживающий данный GSI, либо nullptr.
const arch::x86_64::IoApic* io_apic_for(uint32_t gsi);

uint64_t tsc_hz();
uint64_t apic_timer_hz();  // частота счёта таймера APIC при делителе 16

// Периодический тик таймера APIC текущего CPU с частотой hz (прерывания не
// включает). Каждый тик вызывает sched::on_timer_tick. ticks() считает тики CPU 0.
void start_local_tick(uint32_t hz);
uint64_t ticks();
uint32_t tick_hz();

// Векторы для прерываний устройств (0x30..0xef). 0 — свободных нет.
uint8_t alloc_device_vector(arch::x86_64::InterruptHandler handler);
void free_device_vector(uint8_t vector);

// Монотонное время в наносекундах (по TSC, откалиброванному по HPET).
// Предполагается, что TSC всех CPU идут синхронно (в QEMU это так; проверка
// invariant TSC — после появления SDM Vol. 3B, см. ROADMAP).
uint64_t now_ns();

}  // namespace kernel::platform
