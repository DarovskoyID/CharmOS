#pragma once
#include <stddef.h>
#include <stdint.h>

// USB-IF BOT 1.0, sections 5.1/5.2. SCSI CDBs: Seagate 100293068m.
namespace lib::usb {
struct __attribute__((packed)) Cbw {
  uint32_t signature, tag, bytes;
  uint8_t flags, lun, command_length, command[16];
};
struct __attribute__((packed)) Csw { uint32_t signature, tag, residue; uint8_t status; };
static_assert(sizeof(Cbw) == 31 && offsetof(Cbw, command) == 15);
static_assert(sizeof(Csw) == 13 && offsetof(Csw, status) == 12);
struct StorageInterface { uint8_t number, in, out; uint16_t in_packet, out_packet; };
inline bool storage_interface(const uint8_t* data, size_t size, StorageInterface* out, bool* found) {
  *found = false; *out = {};
  if (data == nullptr || size < 9 || data[0] < 9 || data[1] != 2) return false;
  uint32_t length = uint32_t{data[2]} | (uint32_t{data[3]} << 8);
  if (length > size || length < data[0]) return false;
  bool current = false;
  for (uint32_t at = data[0]; at < length;) {
    if (length - at < 2 || data[at] < 2 || data[at] > length - at) return false;
    if (data[at + 1] == 4) {
      if (data[at] < 9) return false;
      current = !*found && data[at + 3] == 0 && data[at + 5] == 8 && data[at + 6] == 6 && data[at + 7] == 0x50;
      if (current) { *found = true; out->number = data[at + 2]; }
    } else if (data[at + 1] == 5) {
      if (data[at] < 7) return false;
      if (current && (data[at + 3] & 3) == 2) {
        uint16_t packet = static_cast<uint16_t>((uint16_t{data[at + 4]} | (uint16_t{data[at + 5]} << 8)) & 0x7ff);
        if ((data[at + 2] & 15) == 0 || packet == 0 || packet > 1024) return false;
        if ((data[at + 2] & 0x80) != 0) { out->in = data[at + 2]; out->in_packet = packet; }
        else { out->out = data[at + 2]; out->out_packet = packet; }
      }
    }
    at += data[at];
  }
  return !*found || (out->in != 0 && out->out != 0);
}
inline void be32_store(uint8_t* data, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) data[i] = static_cast<uint8_t>(value >> (24 - i * 8));
}
inline uint32_t be32_load(const uint8_t* data) {
  return (uint32_t{data[0]} << 24) | (uint32_t{data[1]} << 16) | (uint32_t{data[2]} << 8) | data[3];
}
}  // namespace lib::usb
