#pragma once
// Виртуальная раскладка ядра (верхняя половина адресного пространства).
// HHDM и база образа заданы boot ABI; остальные окна — внутреннее дело ядра.

#include <abi/boot.hpp>

namespace kernel::mm {

// Прямое отображение физической памяти (только RAM, см. vmm.cpp).
inline constexpr uint64_t kHhdmBase = abi::boot::kHhdmBase;

// Куча ядра: растёт по требованию.
inline constexpr uint64_t kHeapBase = 0xffffc00000000000ull;
inline constexpr uint64_t kHeapMax = 1ull << 30;

// Окно для MMIO-регистров устройств (некэшируемое отображение).
inline constexpr uint64_t kMmioBase = 0xffffd00000000000ull;
inline constexpr uint64_t kMmioMax = 1ull << 30;

// Образ ядра (-mcmodel=kernel, docs/abi/boot.md §3).
inline constexpr uint64_t kKernelBase = abi::boot::kKernelBase;

template <typename T>
T* phys_to_virt(uint64_t phys) {
  return reinterpret_cast<T*>(kHhdmBase + phys);
}

}  // namespace kernel::mm
