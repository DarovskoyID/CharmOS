#pragma once
// Маршрутизация INTx PCI по ACPI (ACPI 6.5 §6.2.13 _PRT, §6.4.2.1 IRQ,
// §6.4.3.6 Extended Interrupt) поверх интерпретатора AML.

#include <stdint.h>

#include <lib/acpi/aml.hpp>

namespace lib::acpi {

struct IntxRoute {
  uint32_t irq;      // GSI, а при isa = true — номер ISA IRQ (его переведёт MADT)
  bool isa;          // из IRQ-дескриптора (линия PIC): GSI — через Interrupt Source Override
  bool level;        // по уровню
  bool active_low;
};

// EisaId("PNPxxxx") в сжатом 32-битном виде (как в _HID).
uint32_t eisa_id(const char id[7]);

// Первый корневой мост PCI (_HID или _CID = PNP0A03/PNP0A08), nullptr — нет.
aml::Node* find_root_bridge(aml::Interpreter& in);

// Линия устройства device (номер на шине корня) для вывода pin (0 — INTA).
bool route_intx(aml::Interpreter& in, aml::Node* bridge, uint8_t device, uint8_t pin, IntxRoute* out);

}  // namespace lib::acpi
