#include <lib/audio/hda.hpp>
#include "test.hpp"
using namespace lib::audio::hda;
TEST(hda_connections_ranges_and_bounds) {
  uint8_t list[8]{}; uint16_t previous = 0; uint32_t count = 0;
  CHECK(connection(2, false, &previous, list, &count, 8));
  CHECK(connection(0x85, false, &previous, list, &count, 8));
  CHECK(count == 4 && list[0] == 2 && list[3] == 5);
  CHECK(connection(200, true, &previous, list, &count, 8));
  CHECK(!connection(0x80ff, true, &previous, list, &count, 8));
  count = 0; previous = 0;
  CHECK(!connection(0x83, false, &previous, list, &count, 8));
  CHECK(!connection(256, true, &previous, list, &count, 8));
  CHECK(verb(2, 3, 0xf00, 9) == 0x203f0009 && kPcm48Stereo16 == 0x11);
}
