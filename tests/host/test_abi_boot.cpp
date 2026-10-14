// Раскладка проверяется static_assert'ами в самом заголовке; этот тест
// гарантирует, что заголовок компилируется и на хосте, и фиксирует константы.
#include <abi/boot.hpp>

#include "test.hpp"

TEST(boot_magic_spells_crmsboot) {
  const uint64_t m = abi::boot::kMagic;
  char s[8];
  std::memcpy(s, &m, sizeof s);
  CHECK(std::memcmp(s, "CRMSBOOT", 8) == 0);
}

TEST(boot_address_constants) {
  CHECK(abi::boot::kKernelBase == 0xffffffff80000000ull);
  CHECK(abi::boot::kHhdmBase == 0xffff800000000000ull);
}
