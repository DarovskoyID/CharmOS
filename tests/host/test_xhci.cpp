#include <lib/usb/xhci.hpp>
#include <lib/usb/hid.hpp>
#include <lib/usb/storage.hpp>
#include <vector>
#include "test.hpp"
using namespace lib::usb::xhci;
namespace {
void collect(void* ctx, const lib::usb::HidEvent& e) {
  static_cast<std::vector<lib::usb::HidEvent>*>(ctx)->push_back(e);
}
}
TEST(usb_boot_reports) {
  lib::usb::BootKeyboard keyboard; std::vector<lib::usb::HidEvent> result;
  uint8_t report[8] = {2, 0, 4, 4, 0, 0, 0, 0};
  CHECK(keyboard.decode(report, 8, collect, &result));
  CHECK(result.size() == 2 && result[0].code == 0xe1 && result[1].code == 4);
  result.clear(); CHECK(keyboard.decode(report, 8, collect, &result) && result.empty());
  report[0] = 0; report[2] = report[3] = 1;
  CHECK(keyboard.decode(report, 8, collect, &result));
  CHECK(result.size() == 1 && result[0].code == 0xe1 && keyboard.keys[4]);
  for (auto& b : report) b = 0;
  result.clear(); CHECK(keyboard.decode(report, 8, collect, &result));
  CHECK(result.size() == 1 && result[0].code == 4 && result[0].value == 0);
  CHECK(!keyboard.decode(report, 7, collect, &result));
  lib::usb::BootMouse mouse; result.clear(); uint8_t packet[] = {1, 0xfd, 9};
  CHECK(mouse.decode(packet, 3, collect, &result));
  CHECK(result.size() == 2 && result[0].x == -3 && result[0].y == 9 && result[1].code == 1 && result[1].value == 1);
}
TEST(usb_configuration_bounds) {
  uint8_t config[] = {9,2,25,0,1,1,0,0x80,50, 9,4,0,0,1,3,1,1,0, 7,5,0x81,3,8,0,10};
  lib::usb::BootInterface out[8]{}; uint32_t count = 0; uint8_t value = 0;
  CHECK(lib::usb::boot_interfaces(config, sizeof config, out, 8, &count, &value));
  CHECK(count == 1 && value == 1 && out[0].endpoint == 0x81 && out[0].max_packet == 8);
  for (size_t n = 0; n < sizeof config; ++n) CHECK(!lib::usb::boot_interfaces(config, n, out, 8, &count, &value));
  config[18] = 0; CHECK(!lib::usb::boot_interfaces(config, sizeof config, out, 8, &count, &value));
}
TEST(usb_storage_descriptor_and_wire_layout) {
  uint8_t data[] = {9,2,32,0,1,1,0,0x80,50, 9,4,0,0,2,8,6,0x50,0, 7,5,0x81,2,0,2,0, 7,5,2,2,0,2,0};
  lib::usb::StorageInterface s{}; bool found = false;
  CHECK(lib::usb::storage_interface(data, sizeof data, &s, &found));
  CHECK(found && s.in == 0x81 && s.out == 2 && s.in_packet == 512 && s.out_packet == 512);
  for (size_t n = 0; n < sizeof data; ++n) CHECK(!lib::usb::storage_interface(data, n, &s, &found));
  uint8_t value[4]{}; lib::usb::be32_store(value, 0x12345678);
  CHECK(value[0] == 0x12 && value[3] == 0x78 && lib::usb::be32_load(value) == 0x12345678);
}
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
