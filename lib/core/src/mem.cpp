// Функции, которые компилятор вправе вызывать сам даже во freestanding-коде.
#include <stddef.h>
#include <stdint.h>

extern "C" {

void* memset(void* dst, int c, size_t n) {
  auto* d = static_cast<unsigned char*>(dst);
  for (size_t i = 0; i < n; ++i) d[i] = static_cast<unsigned char>(c);
  return dst;
}

void* memcpy(void* __restrict dst, const void* __restrict src, size_t n) {
  auto* d = static_cast<unsigned char*>(dst);
  const auto* s = static_cast<const unsigned char*>(src);
  for (size_t i = 0; i < n; ++i) d[i] = s[i];
  return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
  auto* d = static_cast<unsigned char*>(dst);
  const auto* s = static_cast<const unsigned char*>(src);
  if (d < s) {
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
  } else {
    for (size_t i = n; i > 0; --i) d[i - 1] = s[i - 1];
  }
  return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
  const auto* x = static_cast<const unsigned char*>(a);
  const auto* y = static_cast<const unsigned char*>(b);
  for (size_t i = 0; i < n; ++i) {
    if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  }
  return 0;
}

}  // extern "C"
