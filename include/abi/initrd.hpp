#pragma once
// Формат initrd v1.0. Нормативный документ — docs/abi/initrd.md.

#include <stddef.h>
#include <stdint.h>

namespace abi::initrd {

inline constexpr char kMagic[8] = {'C', 'R', 'M', 'S', 'I', 'N', 'I', 'T'};
inline constexpr uint32_t kVersion = 1;
inline constexpr size_t kNameSize = 48;
inline constexpr uint64_t kDataAlign = 16;

struct Header {
  char magic[8];
  uint32_t version;
  uint32_t entry_count;
  uint64_t total_size;
  uint64_t reserved;
};
static_assert(sizeof(Header) == 32);
static_assert(offsetof(Header, total_size) == 16);

struct Entry {
  char name[kNameSize];
  uint64_t offset;
  uint64_t size;
};
static_assert(sizeof(Entry) == 64);
static_assert(offsetof(Entry, offset) == 48);

}  // namespace abi::initrd
