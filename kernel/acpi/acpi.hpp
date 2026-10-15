#pragma once
// ACPI во время работы (M9): пространство имён из DSDT/SSDT на своём
// интерпретаторе AML (lib/acpi/aml.hpp), выключение (_S5, §7.4.2, §16.1.7)
// и перезагрузка (RESET_REG, §4.8.3.6).

#include <lib/acpi/aml.hpp>

namespace kernel::acpi {

// После pci_init: загрузить таблицы, сообщить прошивке режим APIC (\_PIC(1), §5.8.1).
void init();
bool ready();
lib::acpi::aml::Interpreter& interpreter();

// INTx функции PCI (driver.md §10): линия по _PRT корневого моста. false —
// у функции есть MSI/MSI-X, нет вывода или маршрута.
struct Intx {
  uint32_t gsi;
  bool level;
  bool active_low;
};
bool intx(uint32_t pci_index, Intx* out);

// Выключить машину (S5). Возвращается только при неудаче.
void power_off();
// Перезагрузить через RESET_REG. Возвращается только при неудаче.
void reboot();

}  // namespace kernel::acpi
