#pragma once
// Статические таблицы ACPI, нужные ядру в M2. Раскладка — docs/specs/acpi-6.5.pdf
// (RSDP §5.2.5.3 табл. 5.3, заголовок §5.2.6 табл. 5.4, XSDT §5.2.8 табл. 5.8,
// MADT §5.2.12 табл. 5.19–5.30) и docs/specs/hpet-1.0a.pdf (§3.2.4, таблица HPET).
// MCFG описан не в ACPI, а в PCI Firmware Specification — разбирается в M5.

#include <stddef.h>
#include <stdint.h>

namespace lib::acpi {

struct [[gnu::packed]] Rsdp {
  char signature[8];  // "RSD PTR "
  uint8_t checksum;   // сумма байт 0..19 = 0
  char oem_id[6];
  uint8_t revision;   // 0 — ACPI 1.0 (только RSDT), 2+ — есть XSDT
  uint32_t rsdt_address;
  uint32_t length;
  uint64_t xsdt_address;
  uint8_t extended_checksum;  // сумма всех length байт = 0
  uint8_t reserved[3];
};
static_assert(sizeof(Rsdp) == 36);
static_assert(offsetof(Rsdp, revision) == 15);
static_assert(offsetof(Rsdp, xsdt_address) == 24);

struct [[gnu::packed]] SdtHeader {
  char signature[4];
  uint32_t length;  // вся таблица, включая заголовок
  uint8_t revision;
  uint8_t checksum;
  char oem_id[6];
  char oem_table_id[8];
  uint32_t oem_revision;
  uint32_t creator_id;
  uint32_t creator_revision;
};
static_assert(sizeof(SdtHeader) == 36);
static_assert(offsetof(SdtHeader, checksum) == 9);

// MADT: заголовок, адрес Local APIC, флаги, затем записи переменной длины.
struct [[gnu::packed]] MadtHeader {
  SdtHeader header;
  uint32_t local_apic_address;
  uint32_t flags;
};
static_assert(sizeof(MadtHeader) == 44);

inline constexpr uint32_t kMadtPcatCompat = 1u << 0;

enum class MadtType : uint8_t {
  LocalApic = 0,
  IoApic = 1,
  InterruptSourceOverride = 2,
  LocalApicNmi = 4,
  LocalApicAddressOverride = 5,
  LocalX2Apic = 9,
};

struct [[gnu::packed]] MadtEntryHeader {
  uint8_t type;
  uint8_t length;
};

struct [[gnu::packed]] MadtLocalApic {
  MadtEntryHeader h;
  uint8_t processor_uid;
  uint8_t apic_id;
  uint32_t flags;
};
static_assert(sizeof(MadtLocalApic) == 8);

inline constexpr uint32_t kLocalApicEnabled = 1u << 0;
inline constexpr uint32_t kLocalApicOnlineCapable = 1u << 1;

struct [[gnu::packed]] MadtIoApic {
  MadtEntryHeader h;
  uint8_t io_apic_id;
  uint8_t reserved;
  uint32_t address;
  uint32_t gsi_base;
};
static_assert(sizeof(MadtIoApic) == 12);

struct [[gnu::packed]] MadtInterruptOverride {
  MadtEntryHeader h;
  uint8_t bus;     // 0 = ISA
  uint8_t source;  // IRQ шины
  uint32_t gsi;
  uint16_t flags;  // MPS INTI, табл. 5.26
};
static_assert(sizeof(MadtInterruptOverride) == 10);

struct [[gnu::packed]] MadtLocalApicNmi {
  MadtEntryHeader h;
  uint8_t processor_uid;  // 0xff — все процессоры
  uint16_t flags;
  uint8_t lint;
};
static_assert(sizeof(MadtLocalApicNmi) == 6);

struct [[gnu::packed]] MadtLocalApicAddressOverride {
  MadtEntryHeader h;
  uint16_t reserved;
  uint64_t address;
};
static_assert(sizeof(MadtLocalApicAddressOverride) == 12);

struct [[gnu::packed]] MadtLocalX2Apic {
  MadtEntryHeader h;
  uint16_t reserved;
  uint32_t x2apic_id;
  uint32_t flags;
  uint32_t processor_uid;
};
static_assert(sizeof(MadtLocalX2Apic) == 16);

// MPS INTI flags (табл. 5.26): полярность биты 1:0, режим срабатывания 3:2.
inline constexpr uint16_t kIntiPolarityMask = 0x3;
inline constexpr uint16_t kIntiPolarityHigh = 0x1;
inline constexpr uint16_t kIntiPolarityLow = 0x3;
inline constexpr uint16_t kIntiTriggerMask = 0xc;
inline constexpr uint16_t kIntiTriggerEdge = 0x4;
inline constexpr uint16_t kIntiTriggerLevel = 0xc;

// Generic Address Structure (ACPI §5.2.3.2).
struct [[gnu::packed]] GenericAddress {
  uint8_t address_space;  // 0 — системная память
  uint8_t bit_width;
  uint8_t bit_offset;
  uint8_t access_size;
  uint64_t address;
};
static_assert(sizeof(GenericAddress) == 12);

// Таблица HPET (HPET 1.0a §3.2.4).
struct [[gnu::packed]] HpetTable {
  SdtHeader header;
  uint32_t event_timer_block_id;
  GenericAddress base_address;
  uint8_t hpet_number;
  uint16_t minimum_tick;
  uint8_t page_protection;
};
static_assert(sizeof(HpetTable) == 56);
static_assert(offsetof(HpetTable, base_address) == 40);

}  // namespace lib::acpi
