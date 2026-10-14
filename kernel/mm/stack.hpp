#pragma once
// Стеки ядра с защитной страницей снизу: переполнение даёт #PF (а при
// невозможности положить кадр — #DF на стеке IST), а не тихую порчу памяти.

#include <stddef.h>
#include <stdint.h>

#include <lib/core/result.hpp>

#include "vmm.hpp"

namespace kernel::mm {

inline constexpr size_t kKernelStackSize = 32 * 1024;

struct KernelStack {
  uint64_t bottom;  // первый отображённый байт (над защитной страницей)
  uint64_t top;     // вершина: начальный RSP, выровнен на 16
};

// Пока стеки не освобождаются: освобождение требует TLB shootdown на все
// CPU, он появится вместе с планировщиком.
lib::core::Result<KernelStack, VmError> alloc_kernel_stack(size_t size = kKernelStackSize);

// Сколько физических страниц занято всеми стеками ядра (для учёта памяти в тестах).
uint64_t kernel_stack_pages();

}  // namespace kernel::mm
