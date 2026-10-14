#pragma once
// Placement new: конструирует объект в уже выделенной памяти, ничего не
// выделяя (правило 6 CLAUDE.md запрещает только выделяющий operator new).
// <new> во freestanding-ядре недоступен, поэтому объявляем сами.

#include <stddef.h>

inline void* operator new(size_t, void* where) noexcept { return where; }
