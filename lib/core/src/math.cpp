#include <lib/core/math.hpp>

namespace lib::core {

uint64_t mul_div(uint64_t a, uint64_t b, uint64_t c) {
  if (c == 0) __builtin_trap();
  // 128-битное умножение компилятор делает одной инструкцией mul.
  unsigned __int128 p = static_cast<unsigned __int128>(a) * b;
  auto hi = static_cast<uint64_t>(p >> 64);
  auto lo = static_cast<uint64_t>(p);
  if (hi >= c) return ~uint64_t{0};  // частное не помещается в 64 бита

  // Деление 128/64 сдвигом и вычитанием: остаток всегда < c, частное — 64 бита.
  uint64_t rem = hi;
  uint64_t quot = 0;
  for (int i = 63; i >= 0; --i) {
    bool carry = (rem >> 63) != 0;
    rem = (rem << 1) | ((lo >> i) & 1);
    quot <<= 1;
    if (carry || rem >= c) {
      rem -= c;
      quot |= 1;
    }
  }
  return quot;
}

}  // namespace lib::core
