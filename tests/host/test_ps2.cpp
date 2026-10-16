// Клавиатура PS/2: набор 2 → HID Usage (lib/input, источники — в ps2.cpp).
#include <lib/input/ps2.hpp>
#include <lib/input/ring.hpp>
#include <atomic>
#include <chrono>
#include <thread>

#include <vector>

#include "test.hpp"

using namespace lib::input;

TEST(input_ring_notification_handshake) {
  uint32_t head = 0, tail = 1;
  // Reader took tail=1; writer publishes while the queue still appears nonempty.
  CHECK(!publish_tail(&tail, &head, 2));
  CHECK(!consume_head(&head, &tail, 1));  // must drain again rather than sleep
  CHECK(consume_head(&head, &tail, 2));
  CHECK(publish_tail(&tail, &head, 3));   // reader got to sleep first: must wake
  CHECK(consume_head(&head, &tail, 3));
  head = tail = 0xffffffff;
  CHECK(publish_tail(&tail, &head, 0));
  CHECK(consume_head(&head, &tail, 0));
}

TEST(input_ring_concurrent_notifications) {
  constexpr uint32_t n = 100000, capacity = 32;
  uint32_t head = 0, tail = 0, data[capacity]{};
  std::atomic<bool> stop{false}, wakeup{false};
  std::thread writer([&] {
    for (uint32_t i = 0; i < n && !stop.load(); ++i) {
      while (i - __atomic_load_n(&head, __ATOMIC_ACQUIRE) == capacity && !stop.load()) std::this_thread::yield();
      if (stop.load()) break;
      data[i % capacity] = i;
      if (publish_tail(&tail, &head, i + 1)) wakeup.store(true);
    }
  });
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  uint32_t count = 0;
  bool ordered = true, empty = true;
  while (count < n && std::chrono::steady_clock::now() < deadline) {
    if (empty && !wakeup.exchange(false)) { std::this_thread::yield(); continue; }
    uint32_t end = __atomic_load_n(&tail, __ATOMIC_ACQUIRE);
    while (count != end) { ordered &= data[count % capacity] == count; ++count; }
    empty = consume_head(&head, &tail, count);
  }
  stop.store(true); writer.join();
  CHECK(ordered && count == n);
}

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
