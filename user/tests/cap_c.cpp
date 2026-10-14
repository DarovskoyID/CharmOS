// Посторонний: ни одно подобранное число не даёт доступа к чужим объектам.
#include "check.hpp"

using namespace charm;

extern "C" int64_t charm_main(Handle ac) {
  char buf[64];
  Handle hs[4];
  uint32_t n, nh;
  EXPECT(read_msg(ac, buf, sizeof buf, hs, 4, &n, &nh) == Status::Ok);
  EXPECT(same(buf, n, "nothing for you") && nh == 0);

  // Все значения хендлов с поколениями 0..15: у C есть только стартовый канал.
  uint32_t bad = 0, own = 0, other = 0;
  for (uint32_t gen = 0; gen < 16; ++gen) {
    for (uint32_t slot = 0; slot < 1024; ++slot) {
      Handle h = (gen << 10) | slot;
      Status s = vmo_map(h, rights::kRead).status;
      if (s == Status::BadHandle) {
        ++bad;
      } else if (h == ac && s == Status::WrongType) {
        ++own;
      } else {
        ++other;
      }
    }
  }
  EXPECT(own == 1 && other == 0 && bad == 16 * 1024 - 1);
  print("cap_c: 16383 forged handles rejected\n");
  EXPECT(send(ac, "forge-ok") == Status::Ok);
  return 0;
}
