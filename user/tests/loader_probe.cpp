#include <charm/sys.hpp>

namespace {
volatile unsigned char bss[4 * 1024 * 1024 + 17];
volatile uint64_t initialized[] = {0x123456789abcdef0ull, 0xfedcba9876543210ull};
const char literal[] = "eager ELF segment probe";
}

extern "C" int64_t charm_main(charm::Handle) {
  if (initialized[0] != 0x123456789abcdef0ull || initialized[1] != 0xfedcba9876543210ull) return 1;
  const volatile char* text = literal;
  if (text[0] != 'e' || text[sizeof literal - 2] != 'e' || text[sizeof literal - 1] != 0) return 2;
  for (size_t i = 0; i < sizeof bss; i += 4096) {
    if (bss[i] != 0) return 3;
    bss[i] = static_cast<unsigned char>(i / 4096 + 1);
  }
  if (bss[sizeof bss - 1] != 0) return 4;
  bss[sizeof bss - 1] = 0x7c;
  return bss[sizeof bss - 1] == 0x7c ? 73 : 5;
}
