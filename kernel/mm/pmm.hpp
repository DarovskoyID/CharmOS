#pragma once
// Физическая память ядра: битовая карта фреймов поверх карты памяти загрузчика.

#include <abi/boot.hpp>
#include <lib/mm/frame_bitmap.hpp>

namespace kernel::mm {

using lib::mm::FrameError;

// Строит карту фреймов. Свободными становятся только регионы Usable;
// BootloaderReclaimable и AcpiReclaimable пока остаются занятыми
// (ядро ещё читает BootInfo и таблицы ACPI).
void pmm_init(const abi::boot::BootInfo& info);

lib::core::Result<uint64_t, FrameError> alloc_frames(uint64_t count = 1,
                                                     uint64_t align_frames = 1);
// Освобождение чужого или свободного фрейма — паника.
void free_frames(uint64_t phys, uint64_t count = 1);

uint64_t free_frame_count();
uint64_t total_frame_count();

}  // namespace kernel::mm
