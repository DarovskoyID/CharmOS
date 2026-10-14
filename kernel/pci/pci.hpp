#pragma once
// PCI/PCIe: конфигурационное пространство через ECAM, перечисление функций.
//
// ИСТОЧНИК ВТОРИЧНЫЙ. Официальные спецификации PCI-SIG недоступны (журнал
// решений 2026-10-14); раскладки заголовка, BAR и capability взяты из
// docs/specs/pci-reference.md и проверены QEMU-тестами (tests/qemu/m5).
// Сверить с PCI Express Base Specification, если она появится.

#include <abi/boot.hpp>
#include <stdint.h>

namespace kernel::pci {

enum class BarKind : uint8_t { Absent, Mem32, Mem64, Io };

struct Bar {
  BarKind kind;
  bool prefetchable;
  uint64_t base;
  uint64_t size;
};

struct Function {
  uint16_t segment;
  uint8_t bus;
  uint8_t device;
  uint8_t function;
  uint16_t vendor;
  uint16_t device_id;
  uint32_t class_code;  // класс << 16 | подкласс << 8 | prog-if
  uint8_t revision;
  uint8_t header_type;  // без бита multifunction
  Bar bars[6];
  uint8_t msi_cap;   // смещение capability, 0 — нет
  uint8_t msix_cap;
  uint64_t config;   // виртуальный адрес 4 КиБ конфигурационного пространства
  uint64_t msix_table;  // ядерное отображение таблицы MSI-X, 0 — ещё нет
};

inline constexpr uint32_t kMaxFunctions = 256;

// Находит MCFG, отображает окна ECAM, перечисляет функции, определяет BAR.
// Без MCFG — функций ноль (запись в журнал), не паника.
void pci_init(const abi::boot::BootInfo& info);

uint32_t function_count();
const Function& function(uint32_t index);

uint8_t read8(const Function& f, uint16_t offset);
uint16_t read16(const Function& f, uint16_t offset);
uint32_t read32(const Function& f, uint16_t offset);
void write16(const Function& f, uint16_t offset, uint16_t value);
void write32(const Function& f, uint16_t offset, uint32_t value);

// MSI (один вектор). Формат адреса и данных — Intel SDM Vol. 3A §11.11
// (первоисточник); раскладка capability — источник вторичный.
void msi_enable(const Function& f, uint32_t apic_id, uint8_t vector);
void msi_disable(const Function& f);
// Маскирование вектора, если функция его поддерживает; false — не умеет.
bool msi_set_mask(const Function& f, bool masked);
void set_command_bits(const Function& f, uint16_t set, uint16_t clear);

// MSI-X (источник вторичный: pci-reference.md §4). Число векторов таблицы,
// 0 — MSI-X нет.
uint32_t msix_vectors(const Function& f);
// Отображает таблицу (один раз). false — BAR таблицы не память или нет памяти.
bool msix_prepare(uint32_t index);
// Запись таблицы: адрес и данные (формат SDM §11.11); запись остаётся
// замаскированной.
void msix_program(const Function& f, uint32_t entry, uint32_t apic_id, uint8_t vector);
void msix_set_mask(const Function& f, uint32_t entry, bool masked);
// Включает MSI-X (Function Mask снят) и выключает INTx.
void msix_enable(const Function& f);
void msix_disable(const Function& f);
// Страницы BAR bar, где лежат таблица или PBA MSI-X: [first, last] в страницах
// от начала BAR. false — их в этом BAR нет.
bool msix_bar_pages(const Function& f, uint32_t bar, uint64_t* first, uint64_t* last);

// Байты index-й vendor-specific capability (ID 0x09) целиком (cap_len —
// байт 2). false — такой нет. *len — её длина; копируется min(len, cap).
bool vendor_cap(const Function& f, uint32_t index, uint8_t* buf, uint32_t cap, uint32_t* len);

// Смещения заголовка (pci-reference.md §2).
inline constexpr uint16_t kRegVendor = 0x00;
inline constexpr uint16_t kRegCommand = 0x04;
inline constexpr uint16_t kCommandIo = 1u << 0;
inline constexpr uint16_t kCommandMemory = 1u << 1;
inline constexpr uint16_t kCommandBusMaster = 1u << 2;
inline constexpr uint16_t kCommandIntxDisable = 1u << 10;

}  // namespace kernel::pci
