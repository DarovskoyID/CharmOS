#pragma once
// Куча ядра (lib::mm::Heap) в окне kHeapBase. Выделения возвращают Result:
// нехватка памяти — обычная ошибка, а не паника (правило 6 CLAUDE.md).

#include <stddef.h>

#include <lib/mm/heap.hpp>

namespace kernel::mm {

using lib::mm::HeapError;

void heap_init();

lib::core::Result<void*, HeapError> kmalloc(size_t size);
// Освобождение чужого указателя или порча метаданных — паника.
void kfree(void* ptr);

bool heap_check();
size_t heap_used_bytes();
size_t heap_mapped_bytes();

}  // namespace kernel::mm
