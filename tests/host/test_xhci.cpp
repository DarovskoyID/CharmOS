#include <lib/usb/xhci.hpp>
#include "test.hpp"
using namespace lib::usb::xhci;
TEST(xhci_command_ring_cycle_wrap) {
  Trb ring[256]{};
  Producer p{ring, 0x10000, 0, 1};
  for (uint32_t i = 0; i < 3000; ++i) {
    uint32_t index = i % 255, cycle = 1 ^ ((i / 255) & 1);
    CHECK(p.push({i, 0, type(kNoop)}) == 0x10000 + index * 16);
    CHECK(ring[index].parameter == i && ring[index].control == (type(kNoop) | cycle));
    if (index == 254) {
      CHECK(ring[255].parameter == 0x10000 && ring[255].control == (type(kLink) | 2 | cycle));
      CHECK(p.index == 0 && p.cycle == (cycle ^ 1));
    }
  }
}
TEST(xhci_event_ring_cycle_wrap) {
  Trb ring[256]{}; Consumer c{ring, 0x20000, 0, 1}; Trb out{};
  CHECK(!c.pop(&out));
  for (uint32_t i = 0; i < 1024; ++i) {
    uint32_t index = i % 256, cycle = 1 ^ ((i / 256) & 1);
    ring[index] = {i, 1u << 24, type(kCommandCompletion) | cycle};
    CHECK(c.pop(&out) && out.parameter == i && out.status == (1u << 24));
    CHECK(c.dequeue() == 0x20000 + ((i + 1) % 256) * 16);
    CHECK(!c.pop(&out));
  }
}
