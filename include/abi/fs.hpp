#pragma once
// Файлы и каталоги, syscall ABI v1.3. Нормативный документ — docs/abi/fs.md.

#include <stddef.h>
#include <stdint.h>

namespace abi::fs {

// §4: флаги fs_open.
inline constexpr uint64_t kOpenWrite = 1u << 0;
inline constexpr uint64_t kOpenCreate = 1u << 1;
inline constexpr uint64_t kOpenExclusive = 1u << 2;
inline constexpr uint64_t kOpenTruncate = 1u << 3;
inline constexpr uint64_t kOpenDirectory = 1u << 4;
inline constexpr uint64_t kOpenAll = 0x1f;

inline constexpr uint64_t kMaxPath = 4096;
inline constexpr uint64_t kMaxComponent = 255;
inline constexpr uint64_t kMaxTransfer = 1u << 20;  // file_read/file_write за вызов

inline constexpr uint32_t kTypeFile = 1;
inline constexpr uint32_t kTypeDirectory = 2;

// §4.1
struct Stat {
  uint64_t size;
  uint32_t type;
  uint32_t reserved;
  uint64_t mtime_ns;
  uint64_t reserved2;
};
static_assert(sizeof(Stat) == 32);
static_assert(offsetof(Stat, type) == 8);
static_assert(offsetof(Stat, mtime_ns) == 16);

// §4.2: заголовок записи dir_read, за ним имя; запись выровнена на 8.
struct DirEntryHeader {
  uint16_t record_len;
  uint8_t type;
  uint8_t name_len;
};
static_assert(sizeof(DirEntryHeader) == 4);

}  // namespace abi::fs
