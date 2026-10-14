#pragma once
// Таблицы страниц ядра. После vmm_init ядро работает на своих таблицах:
// образ с правами по секциям (W^X), HHDM только для RAM, без тождественного
// отображения нижней памяти (контракт docs/abi/boot.md §4 требует его снять).

#include <abi/boot.hpp>
#include <lib/core/result.hpp>

namespace kernel::mm {

enum class VmError : uint8_t { OutOfMemory, AlreadyMapped, OutOfAddressSpace };

void vmm_init(const abi::boot::BootInfo& info);

// Физ. адрес PML4 ядра (для CR3 новых CPU).
uint64_t kernel_root();

// Отображает свежий обнулённый фрейм по адресу virt (RW, NX).
lib::core::Status<VmError> map_new_page(uint64_t virt);

// Отображает физический диапазон MMIO в окно kMmioBase: некэшируемо, RW, NX.
lib::core::Result<uint64_t, VmError> map_mmio(uint64_t phys, uint64_t size);

// Для тестов и диагностики: физ. адрес и флаги отображения.
bool translate(uint64_t virt, uint64_t* phys, uint64_t* flags);

}  // namespace kernel::mm
