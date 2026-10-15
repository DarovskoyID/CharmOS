#pragma once
// GPT (UEFI 2.11 §5.2.3 — защитный MBR, §5.3 — заголовок и массив записей).
// Работает с секторами по 512 байт.

#include <stddef.h>
#include <stdint.h>

#include <lib/fs/fs.hpp>

namespace lib::fs {

// CRC32 по UEFI 2.11 §4.2.1 («стандартный CCITT32», полином 0x04C11DB7):
// отражённый, начальное и конечное значение ~0.
uint32_t crc32(const void* data, size_t len, uint32_t crc = 0);

struct Guid {
  uint8_t bytes[16];  // как на диске
};

bool operator==(const Guid& a, const Guid& b);

// Тип раздела EFI System Partition (UEFI 2.11 табл. 5.7).
extern const Guid kGuidEsp;
// Тип раздела «данные Linux» (0FC63DAF-8483-4772-8E79-3D69D8477DE4) —
// источник вторичный: в UEFI не определён, общепринятое значение.
extern const Guid kGuidLinuxData;
// Закрытая системная область CharmOS (docs/abi/pkg.md §1):
// C4A12F0E-6A3B-4D52-9E3A-43484152534F.
extern const Guid kGuidSystemArea;

struct GptPartition {
  Guid type;
  Guid unique;
  uint64_t first_lba;
  uint64_t last_lba;  // включительно
};

struct GptInfo {
  static constexpr size_t kMax = 16;
  size_t count;  // непустые записи по порядку
  GptPartition parts[kMax];
};

// Читает основной GPT (LBA 1), проверяет подписи и обе CRC. Резервную копию
// не использует (в v1.3 — ошибка Corrupt, если основная испорчена).
Status read_gpt(Io& disk, GptInfo* out);

// Пишет защитный MBR, основной и резервный GPT. Разделы — по порядку, в
// пределах [34, sectors − 34).
Status write_gpt(Io& disk, const GptPartition* parts, size_t count, const Guid& disk_guid);

}  // namespace lib::fs
