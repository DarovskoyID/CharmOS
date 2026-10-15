#pragma once
// Пакет программы .cpk, v1 (docs/abi/package.md). Все числа — little-endian.

#include <stdint.h>

namespace abi::package {

inline constexpr char kMagic[4] = {'C', 'P', 'K', '1'};
inline constexpr uint16_t kVersion = 1;
inline constexpr uint32_t kMaxFiles = 4096;
inline constexpr uint32_t kMaxManifest = 64 * 1024;
inline constexpr uint32_t kMaxPath = 256;
inline constexpr uint32_t kSignatureBlock = 96;  // подпись Ed25519 (64) + открытый ключ (32)

// §1
struct Header {
  char magic[4];
  uint16_t version;
  uint16_t flags;
  uint32_t file_count;
  uint32_t manifest_size;
  uint64_t table_offset;
  uint64_t manifest_offset;
  uint64_t data_offset;
  uint64_t signed_size;       // = data_offset (§5, уточнение §7)
  uint64_t signature_offset;  // 0 — пакет без подписи
  uint64_t reserved;
};
static_assert(sizeof(Header) == 64);
static_assert(__builtin_offsetof(Header, file_count) == 8);
static_assert(__builtin_offsetof(Header, manifest_size) == 12);
static_assert(__builtin_offsetof(Header, table_offset) == 16);
static_assert(__builtin_offsetof(Header, manifest_offset) == 24);
static_assert(__builtin_offsetof(Header, data_offset) == 32);
static_assert(__builtin_offsetof(Header, signed_size) == 40);
static_assert(__builtin_offsetof(Header, signature_offset) == 48);

// §2
struct FileEntry {
  char path[kMaxPath];
  uint64_t offset;
  uint64_t size;
  uint32_t mode;
  uint32_t reserved;
  uint8_t sha512[64];
};
static_assert(sizeof(FileEntry) == 344);
static_assert(__builtin_offsetof(FileEntry, offset) == 256);
static_assert(__builtin_offsetof(FileEntry, size) == 264);
static_assert(__builtin_offsetof(FileEntry, mode) == 272);
static_assert(__builtin_offsetof(FileEntry, sha512) == 280);
inline constexpr uint32_t kModeExec = 1u << 0;

// §4: права манифеста.
inline constexpr uint32_t kPermNet = 1u << 0;
inline constexpr uint32_t kPermNetListen = 1u << 1;
inline constexpr uint32_t kPermFiles = 1u << 2;
inline constexpr uint32_t kPermHome = 1u << 3;
inline constexpr uint32_t kPermConsole = 1u << 4;

}  // namespace abi::package
