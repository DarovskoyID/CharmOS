#include <lib/core/format.hpp>

namespace lib::core {

size_t format_hex64(char* buf, size_t cap, uint64_t value) {
  if (cap < kHex64Len) return 0;
  constexpr char kDigits[] = "0123456789abcdef";
  buf[0] = '0';
  buf[1] = 'x';
  for (size_t i = 0; i < 16; ++i) {
    buf[kHex64Len - 1 - i] = kDigits[value & 0xf];
    value >>= 4;
  }
  return kHex64Len;
}

size_t format_dec(char* buf, size_t cap, uint64_t value) {
  char tmp[20];
  size_t n = 0;
  do {
    tmp[n++] = static_cast<char>('0' + value % 10);
    value /= 10;
  } while (value != 0);
  if (n > cap) return 0;
  for (size_t i = 0; i < n; ++i) buf[i] = tmp[n - 1 - i];
  return n;
}

}  // namespace lib::core
