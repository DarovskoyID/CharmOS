// Функции, которые компилятор вправе вызывать сам даже во freestanding-коде.
#include <stddef.h>
#include <stdint.h>

extern "C" {
#if defined(CHARMOS_KERNEL_MEMORY)
void* charmos_x86_memset(void*, int, size_t);
void* charmos_x86_memcpy(void*, const void*, size_t);
#endif

void* memset(void* dst, int c, size_t n) {
#if defined(CHARMOS_KERNEL_MEMORY)
  return charmos_x86_memset(dst, c, n);
#else
  auto* d = static_cast<unsigned char*>(dst);
  for (size_t i = 0; i < n; ++i) d[i] = static_cast<unsigned char>(c);
  return dst;
#endif
}

void* memcpy(void* __restrict dst, const void* __restrict src, size_t n) {
#if defined(CHARMOS_KERNEL_MEMORY)
  return charmos_x86_memcpy(dst, src, n);
#else
  auto* d = static_cast<unsigned char*>(dst);
  const auto* s = static_cast<const unsigned char*>(src);
  for (size_t i = 0; i < n; ++i) d[i] = s[i];
  return dst;
#endif
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
