#pragma once
#include <stddef.h>
#include <stdint.h>

// USB 2.0 chapter 9; HID 1.11 appendices B/C and Set_Protocol section 7.2.6.
namespace lib::usb {
struct BootInterface {
  uint8_t number, protocol, endpoint, interval;
  uint16_t max_packet;
};
inline bool boot_interfaces(const uint8_t* data, size_t bytes, BootInterface* out, uint32_t capacity,
                            uint32_t* count, uint8_t* configuration) {
  *count = 0;
  if (data == nullptr || bytes < 9 || data[0] < 9 || data[1] != 2) return false;
  uint32_t length = uint32_t{data[2]} | (uint32_t{data[3]} << 8);
  if (length < data[0] || length > bytes) return false;
  *configuration = data[5];
  if (*configuration == 0) return false;
  int current = -1;
  for (uint32_t at = data[0]; at < length;) {
    if (length - at < 2 || data[at] < 2 || data[at] > length - at) return false;
    uint8_t size = data[at], type = data[at + 1];
    if (type == 4) {
      if (size < 9) return false;
      current = -1;
      if (data[at + 3] == 0 && data[at + 5] == 3 && data[at + 6] == 1 && (data[at + 7] == 1 || data[at + 7] == 2)) {
        if (*count == capacity) return false;
        current = static_cast<int>((*count)++);
        out[current] = {data[at + 2], data[at + 7], 0, 0, 0};
      }
    } else if (type == 5) {
      if (size < 7) return false;
      if (current >= 0 && (data[at + 2] & 0x80) != 0 && (data[at + 3] & 3) == 3 && out[current].endpoint == 0) {
        uint16_t packet = static_cast<uint16_t>((uint16_t{data[at + 4]} | (uint16_t{data[at + 5]} << 8)) & 0x7ff);
        if ((data[at + 2] & 15) == 0 || packet == 0 || packet > 1024 || data[at + 6] == 0) return false;
        out[current].endpoint = data[at + 2]; out[current].interval = data[at + 6]; out[current].max_packet = packet;
      }
    }
    at += size;
  }
  for (uint32_t i = 0; i < *count; ++i) if (out[i].endpoint == 0) return false;
  return true;
}
struct HidEvent { uint16_t type, code; int32_t value, x, y; };
using Emit = void (*)(void*, const HidEvent&);
struct BootKeyboard {
  bool keys[256]{};
  uint8_t modifiers = 0;
  bool decode(const uint8_t* report, size_t bytes, Emit emit, void* context) {
    if (report == nullptr || bytes < 8) return false;
    for (unsigned i = 0; i < 8; ++i) if (((report[0] ^ modifiers) & (1u << i)) != 0)
      emit(context, {1, static_cast<uint16_t>(0xe0 + i), (report[0] >> i) & 1, 0, 0});
    modifiers = report[0];
    for (unsigned i = 2; i < 8; ++i) if (report[i] >= 1 && report[i] <= 3) return true; // rollover: keep keys
    bool next[256]{};
    for (unsigned i = 2; i < 8; ++i) if (report[i] >= 4 && report[i] < 0xe0) next[report[i]] = true;
    for (uint16_t i = 4; i < 0xe0; ++i) if (keys[i] != next[i]) emit(context, {1, i, next[i] ? 1 : 0, 0, 0});
    for (unsigned i = 0; i < 256; ++i) keys[i] = next[i];
    return true;
  }
};
struct BootMouse {
  uint8_t buttons = 0;
  bool decode(const uint8_t* report, size_t bytes, Emit emit, void* context) {
    if (report == nullptr || bytes < 3) return false;
    int32_t dx = static_cast<int8_t>(report[1]), dy = static_cast<int8_t>(report[2]);
    if (dx != 0 || dy != 0) emit(context, {2, 0, 0, dx, dy});
    for (uint16_t i = 0; i < 3; ++i) if (((report[0] ^ buttons) & (1u << i)) != 0)
      emit(context, {3, static_cast<uint16_t>(i + 1), (report[0] >> i) & 1, 0, 0});
    buttons = report[0] & 7;
    return true;
  }
};
}  // namespace lib::usb
