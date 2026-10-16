#pragma once
#include <stddef.h>
#include <stdint.h>
namespace lib::audio::hda {
// Intel HDA 1.0a, 3.6.3 and 3.7.1.
struct Bdle { uint64_t address; uint32_t length, flags; };
static_assert(sizeof(Bdle) == 16 && offsetof(Bdle, flags) == 12);
inline constexpr uint16_t kPcm48Stereo16 = 0x11;
inline constexpr uint32_t verb(uint32_t codec, uint32_t node, uint32_t id, uint32_t payload) {
  return (codec << 28) | (node << 20) | (id << 8) | payload;
}
// Expand one raw connection-list entry (7.3.3.3), retaining its predecessor
// across response dwords. Nid is bounded to the eight-bit command field.
inline bool connection(uint16_t raw, bool long_form, uint16_t* previous, uint8_t* out, uint32_t* count, uint32_t capacity) {
  uint16_t range_bit = long_form ? 0x8000 : 0x80;
  if (*count > capacity) return false;
  uint16_t node = raw & static_cast<uint16_t>(range_bit - 1);
  if (node == 0 || node > 255) return false;
  uint32_t start = node;
  if ((raw & range_bit) != 0) {
    if (*count == 0 || node <= *previous) return false;
    start = uint32_t{*previous} + 1;
  }
  if (node - start + 1 > capacity - *count) return false;
  for (uint32_t n = start; n <= node; ++n) out[(*count)++] = static_cast<uint8_t>(n);
  *previous = node;
  return true;
}
}  // namespace lib::audio::hda
