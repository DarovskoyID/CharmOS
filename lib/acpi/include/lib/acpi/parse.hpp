#pragma once
// Разбор статических таблиц ACPI. Функции работают с уже отображёнными
// байтами и не знают, как ядро отображает физическую память: таблицы,
// на которые ссылается XSDT, достаются через MapFn.

#include <stddef.h>
#include <stdint.h>

#include <lib/acpi/tables.hpp>
#include <lib/core/result.hpp>
#include <lib/core/string_view.hpp>

namespace lib::acpi {

enum class AcpiError : uint8_t {
  BadSignature,
  BadChecksum,
  BadLength,
  NoXsdt,        // RSDP ревизии 0 (ACPI 1.0) не поддерживаем
  NotFound,
  MapFailed,
  TooMany,       // в MADT больше записей, чем помещается в MadtInfo
  Unsupported,
};

const char* error_name(AcpiError e);

bool checksum_ok(const void* data, size_t length);

// Проверяет RSDP (обе контрольные суммы) и возвращает физ. адрес XSDT.
lib::core::Result<uint64_t, AcpiError> parse_rsdp(const void* rsdp);

// Проверяет таблицу целиком: сигнатуру (если задана), длину и контрольную сумму.
// available — сколько байт по адресу гарантированно отображено.
lib::core::Status<AcpiError> validate_table(const void* table, size_t available,
                                            lib::core::StringView signature);

// Отображает физ. диапазон и возвращает указатель, либо nullptr.
using MapFn = const void* (*)(void* ctx, uint64_t phys, size_t size);

// Ищет в XSDT таблицу с сигнатурой (index — какая по счёту, с нуля),
// проверяет её и возвращает указатель на всю таблицу.
lib::core::Result<const SdtHeader*, AcpiError> find_table(const SdtHeader* xsdt,
                                                          lib::core::StringView signature,
                                                          size_t index, MapFn map, void* ctx);

struct CpuInfo {
  uint32_t apic_id;
  uint32_t processor_uid;
  bool enabled;
  bool online_capable;
};

struct IoApicInfo {
  uint8_t id;
  uint32_t address;
  uint32_t gsi_base;
};

struct InterruptOverride {
  uint8_t source;  // ISA IRQ
  uint32_t gsi;
  uint16_t flags;  // MPS INTI
};

struct MadtInfo {
  static constexpr size_t kMaxCpus = 256;
  static constexpr size_t kMaxIoApics = 16;
  static constexpr size_t kMaxOverrides = 16;

  uint64_t local_apic_address;
  bool pcat_compat;
  size_t cpu_count;
  CpuInfo cpus[kMaxCpus];
  size_t io_apic_count;
  IoApicInfo io_apics[kMaxIoApics];
  size_t override_count;
  InterruptOverride overrides[kMaxOverrides];
  // LINT, на который заведён NMI (0xff — не указан).
  uint8_t nmi_lint;
  uint16_t nmi_flags;
};

// Таблица уже проверена validate_table/find_table.
lib::core::Status<AcpiError> parse_madt(const SdtHeader* madt, MadtInfo* out);

// GSI для ISA IRQ с учётом переопределений; flags — MPS INTI (0 — по умолчанию
// шины ISA: фронт, высокий уровень).
uint32_t isa_irq_to_gsi(const MadtInfo& madt, uint8_t irq, uint16_t* flags);

struct HpetInfo {
  uint64_t base_address;
  uint8_t hpet_number;
  uint16_t minimum_tick;
};

lib::core::Status<AcpiError> parse_hpet(const SdtHeader* hpet, HpetInfo* out);

}  // namespace lib::acpi

namespace lib::acpi {

// FADT (ACPI §5.2.9, табл. 5.9) — только поля, нужные M2. Поля за пределами
// длины таблицы (старые ревизии) остаются нулевыми.
struct FadtInfo {
  uint8_t revision;
  uint16_t sci_interrupt;   // смещение 46
  uint16_t iapc_boot_arch;  // смещение 109
  uint32_t flags;           // смещение 112
};

lib::core::Status<AcpiError> parse_fadt(const SdtHeader* fadt, FadtInfo* out);

}  // namespace lib::acpi

namespace lib::acpi {

// MCFG — окна ECAM (источник вторичный: docs/specs/pci-reference.md §1; в ACPI
// 6.5 таблица только упомянута со ссылкой на PCI Firmware Specification).
// Заголовок 36 байт, 8 байт резерва, затем записи по 16 байт.
struct EcamWindow {
  uint64_t base;
  uint16_t segment;
  uint8_t start_bus;
  uint8_t end_bus;
};

struct McfgInfo {
  static constexpr size_t kMaxWindows = 8;
  size_t count;
  EcamWindow windows[kMaxWindows];
};

lib::core::Status<AcpiError> parse_mcfg(const SdtHeader* mcfg, McfgInfo* out);

}  // namespace lib::acpi
