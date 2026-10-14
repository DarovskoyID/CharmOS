#pragma once
// Целочисленная арифметика без библиотеки компилятора: в ядре нет
// compiler-rt, а 128-битное деление требует __udivti3.

#include <stdint.h>

namespace lib::core {

// floor(a * b / c) без переполнения промежуточного произведения.
// c != 0; если результат не помещается в 64 бита — возвращает UINT64_MAX.
uint64_t mul_div(uint64_t a, uint64_t b, uint64_t c);

}  // namespace lib::core
