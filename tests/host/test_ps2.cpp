// Клавиатура PS/2: набор 2 → HID Usage (lib/input, источники — в ps2.cpp).
#include <lib/input/ps2.hpp>

#include <vector>

#include "test.hpp"

using namespace lib::input;

namespace {

std::vector<KeyEvent> run(std::vector<uint8_t> bytes) {
  Set2Decoder d;
  std::vector<KeyEvent> out;
  KeyEvent e{};
  for (uint8_t b : bytes) {
    if (d.feed(b, &e)) out.push_back(e);
  }
  return out;
}

bool is(const std::vector<KeyEvent>& v, size_t i, uint16_t usage, bool pressed) {
  return i < v.size() && v[i].usage == usage && v[i].pressed == pressed;
}

}  // namespace

TEST(ps2_basic_keys) {
  // «A» (клавиша 31, 1C) нажата и отпущена; Enter (43, 5A); Esc (110, 76).
  auto v = run({0x1c, 0xf0, 0x1c, 0x5a, 0x76});
  CHECK(v.size() == 4);
  CHECK(is(v, 0, 0x04, true) && is(v, 1, 0x04, false) && is(v, 2, 0x28, true) && is(v, 3, 0x29, true));
}

TEST(ps2_extended_and_fake_shift) {
  // Стрелка вверх (83) с Num Lock: E0 12 E0 75 … E0 F0 75 E0 F0 12 — ложный Shift отброшен.
  auto v = run({0xe0, 0x12, 0xe0, 0x75, 0xe0, 0xf0, 0x75, 0xe0, 0xf0, 0x12});
  CHECK(v.size() == 2 && is(v, 0, 0x52, true) && is(v, 1, 0x52, false));
  // Правый Alt (62, E0 11) и левый Alt (60, 11) различаются.
  auto a = run({0xe0, 0x11, 0x11});
  CHECK(a.size() == 2 && is(a, 0, 0xe6, true) && is(a, 1, 0xe2, true));
}

TEST(ps2_pause) {
  auto v = run({0xe1, 0x14, 0x77, 0xe1, 0xf0, 0x14, 0xf0, 0x77, 0x1c});
  CHECK(v.size() == 2 && is(v, 0, 0x48, true) && is(v, 1, 0x04, true));
}
