#pragma once
// IOMMU Intel VT-d (docs/specs/vtd-5.2.pdf): трансляция DMA устройств PCI в
// legacy-режиме (Root/Context-таблицы, §9.1, §9.3) с таблицами второго уровня
// (§9.8). Очереди инвалидации и переназначение прерываний не используются:
// инвалидация — через регистры (§11.4.6), MSI идут в формате совместимости.
//
// Домен — набор отображений IOVA → физ. адрес; устройство, привязанное к
// домену, видит только их. Устройство без домена (контекст не заполнен) не
// может обращаться к памяти вовсе: ошибка трансляции (§7).

#include <abi/boot.hpp>
#include <stdint.h>

#include "../core/cmdline.hpp"

namespace kernel::pci::iommu {

// После pci_init. Находит DMAR, включает трансляцию на всех блоках. Без DMAR
// или при iommu=off трансляции нет (active() == false).
void init(const abi::boot::BootInfo& info, const Cmdline& cmdline);

bool active();
// iommu=off в командной строке: DMA без изоляции разрешён явно
// (docs/abi/driver.md §5).
bool disabled_by_cmdline();

struct Domain;

// nullptr — нет памяти или идентификаторов доменов.
Domain* domain_create();
// Отвязывает устройства домена и освобождает его таблицы.
void domain_destroy(Domain* d);

// Привязывает функцию PCI к домену (прежняя привязка снимается). false —
// функцию не обслуживает ни один блок IOMMU.
bool attach(Domain* d, uint32_t pci_index);

// Выделяет IOVA под pages страниц и отображает frames[i] (4 КиБ) подряд.
// 0 — нет памяти под таблицы или адресов. writable — устройство может писать.
uint64_t map(Domain* d, const uint64_t* frames, uint64_t pages, bool writable);
// Снимает отображение и ждёт, пока IOMMU забудет его (IOTLB, с осушением DMA
// там, где блок это умеет): после возврата фреймы можно освобождать.
void unmap(Domain* d, uint64_t iova, uint64_t pages);

// Вызывается из обработчика прерывания ошибок (без удерживаемых блокировок)
// для каждой записанной ошибки трансляции.
using FaultHandler = void (*)(uint32_t pci_index, uint64_t address, uint8_t reason);
void set_fault_handler(FaultHandler handler);

uint64_t fault_count();

}  // namespace kernel::pci::iommu
