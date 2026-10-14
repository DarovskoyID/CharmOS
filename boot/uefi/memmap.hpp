#pragma once
// Перевод карты памяти UEFI в abi::boot::MemoryRegion (docs/abi/boot.md §5).
// Чистая функция: не вызывает прошивку, тестируется на хосте.

#include <abi/boot.hpp>

#include "efi.hpp"

namespace boot {

abi::boot::MemoryType convert_memory_type(uint32_t efi_type);

// Переводит efi_count дескрипторов (шаг descriptor_size байт) в out,
// добавляет регион фреймбуфера (если fb_size != 0 и он не пересекается
// с картой), сортирует по base и сливает соседние регионы одного типа.
// Возвращает число регионов или 0, если не хватило out_cap.
size_t convert_memory_map(const uint8_t* efi_map, size_t efi_count, size_t descriptor_size,
                          uint64_t fb_base, uint64_t fb_size, abi::boot::MemoryRegion* out,
                          size_t out_cap);

}  // namespace boot
