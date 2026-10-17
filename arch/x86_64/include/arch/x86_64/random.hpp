#pragma once
#include <stdint.h>

namespace arch::x86_64 {
// Intel SDM Vol. 2: CPUID.01H:ECX.RDRAND[30], RDRAND and CF.
inline bool has_rdrand() {
  uint32_t a = 1, b, c = 0, d;
  asm volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
  return (c & (1u << 30)) != 0;
}
inline bool random_word(uint64_t* out) {
  unsigned char valid;
  uint64_t word;
  asm volatile("rdrand %0; setc %1" : "=r"(word), "=qm"(valid) : : "cc");
  if (valid) *out = word;
  return valid != 0;
}
}
