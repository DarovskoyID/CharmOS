// M6: вторая загрузка с тем же образом — данные fs_test на месте.
#include "check.hpp"

using namespace charm;
namespace { uint8_t g_bulk[8193]; }

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
  auto bulk = fs_open(hs[0], "persist-bulk.bin", 0);
  EXPECT(bulk.ok());
  for (uint32_t offset = 0; offset < 200 * 1024;) {
    uint32_t count = 200 * 1024 - offset;
    if (count > sizeof g_bulk) count = sizeof g_bulk;
    auto part = file_read(static_cast<Handle>(bulk.value), g_bulk, count, offset);
    EXPECT(part.ok() && part.value == count);
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t source = offset + i;
      if (source >= 4093 && source < 4093 + 8001) source = source - 4093 + 7;
      EXPECT(g_bulk[i] == static_cast<uint8_t>(source * 13 + (source >> 10)));
    }
    offset += count;
  }
  EXPECT(handle_close(static_cast<Handle>(bulk.value)) == Status::Ok);
  print("fs_check: data survived reboot\n");
  return 0;
}
