// M6: вторая загрузка с тем же образом — данные fs_test на месте.
#include "check.hpp"

using namespace charm;

extern "C" int64_t charm_main(Handle start) {
  char msg[8];
  Handle hs[1];
  uint32_t n, nh;
  EXPECT(read_msg(start, msg, sizeof msg, hs, 1, &n, &nh) == Status::Ok);
  EXPECT(same(msg, n, "root") && nh == 1);
  auto p = fs_open(hs[0], "persist.txt", 0);
  EXPECT(p.ok());
  char text[32];
  auto r = file_read(static_cast<Handle>(p.value), text, sizeof text, 0);
  EXPECT(r.ok() && same(text, static_cast<uint32_t>(r.value), "survives reboot"));
  EXPECT(fs_open(hs[0], "work", abi::fs::kOpenDirectory).status == Status::NotFound);
  print("fs_check: data survived reboot\n");
  return 0;
}
