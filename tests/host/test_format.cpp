#include <lib/core/format.hpp>

#include "test.hpp"

using lib::core::format_dec;
using lib::core::format_hex64;
using lib::core::kHex64Len;

TEST(hex_zero) {
  char buf[kHex64Len];
  size_t n = format_hex64(buf, sizeof buf, 0);
  CHECK_STR(buf, n, "0x0000000000000000");
}

TEST(hex_max) {
  char buf[kHex64Len];
  size_t n = format_hex64(buf, sizeof buf, ~uint64_t{0});
  CHECK_STR(buf, n, "0xffffffffffffffff");
}

TEST(hex_pattern) {
  char buf[kHex64Len];
  size_t n = format_hex64(buf, sizeof buf, 0x0123456789abcdef);
  CHECK_STR(buf, n, "0x0123456789abcdef");
}

TEST(hex_too_small) {
  char buf[kHex64Len - 1];
  CHECK(format_hex64(buf, sizeof buf, 1) == 0);
}

TEST(dec_values) {
  char buf[20];
  size_t n = format_dec(buf, sizeof buf, 0);
  CHECK_STR(buf, n, "0");
  n = format_dec(buf, sizeof buf, 1234567890);
  CHECK_STR(buf, n, "1234567890");
  n = format_dec(buf, sizeof buf, ~uint64_t{0});
  CHECK_STR(buf, n, "18446744073709551615");
}

TEST(dec_too_small) {
  char buf[3];
  CHECK(format_dec(buf, sizeof buf, 1000) == 0);
  CHECK(format_dec(buf, sizeof buf, 999) == 3);
}
