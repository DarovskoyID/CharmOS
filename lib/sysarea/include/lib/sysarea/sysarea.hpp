#pragma once
// Закрытая область на диске (docs/abi/sysarea-crypt.md, v1): заголовок и
// XtsIo — вид области данных, расшифрованный для ФС. Общее для ядра,
// mkdisk и хост-тестов. TPM здесь нет: ключ приходит снаружи.

#include <stddef.h>
#include <stdint.h>

#include <lib/crypto/aes.hpp>
#include <lib/fs/fs.hpp>

namespace lib::sysarea {

inline constexpr size_t kHeaderSize = 4096;
inline constexpr uint64_t kDataOffset = 1024 * 1024;
inline constexpr size_t kUnit = 512;
inline constexpr size_t kKeySize = 64;
inline constexpr char kMagic[8] = {'C', 'H', 'A', 'R', 'M', 'C', 'R', 'Y'};
inline constexpr uint16_t kVersion = 1;
inline constexpr uint16_t kCipherAes256Xts = 1;
inline constexpr uint32_t kPcrMask = 0xff;   // PCR 0–7 (журнал решений 2026-10-15)
inline constexpr uint16_t kPcrSha256 = 0x000b;  // TPM_ALG_SHA256
inline constexpr size_t kCheckOffset = 4032;
inline constexpr size_t kSealedMax = kCheckOffset - 64;  // public + private

enum class State : uint32_t { Plain = 1, Encrypting = 2, Encrypted = 3 };

// §2. Поля до смещения 64; дальше — public | private, в конце — SHA-512.
struct Fields {
  char magic[8];
  uint16_t version;
  uint16_t cipher;
  uint32_t state;
  uint64_t data_offset;
  uint64_t data_units;
  uint64_t progress;
  uint32_t pcr_mask;
  uint16_t pcr_hash;
  uint16_t public_size;
  uint16_t private_size;
  uint8_t reserved[14];
};
static_assert(sizeof(Fields) == 64);
static_assert(__builtin_offsetof(Fields, state) == 12 && __builtin_offsetof(Fields, data_units) == 24);
static_assert(__builtin_offsetof(Fields, pcr_mask) == 40 && __builtin_offsetof(Fields, public_size) == 46);

struct Header {
  Fields f;
  uint8_t sealed[kSealedMax];
  uint8_t check[64];
};
static_assert(sizeof(Header) == kHeaderSize);
static_assert(__builtin_offsetof(Header, check) == kCheckOffset);

// Заголовок открытой области (state = Plain) для раздела size байт.
void make_plain(Header* h, uint64_t partition_bytes);
// Пересчитать check (после любого изменения полей).
void seal_check(Header* h);
// magic, version, cipher, check и согласованность размеров с разделом.
bool valid(const Header& h, uint64_t partition_bytes);

// Область данных раздела part как Io для ФС. Единицы с номером ниже limit
// шифруются, остальные — как есть (первичное шифрование, §4). Длина и
// смещения запросов — любые: неполная единица читается и переписывается.
class XtsIo final : public lib::fs::Io {
 public:
  void init(lib::fs::Io* part, const lib::crypto::Xts256* key, uint64_t data_units, uint64_t limit);
  void set_limit(uint64_t limit) { limit_ = limit; }
  lib::fs::Status read(uint64_t offset, void* buf, size_t len) override;
  lib::fs::Status write(uint64_t offset, const void* buf, size_t len) override;
  lib::fs::Status flush() override { return part_->flush(); }
  uint64_t size() const override { return units_ * kUnit; }

 private:
  lib::fs::Status read_unit(uint64_t unit, uint8_t out[kUnit]);
  lib::fs::Status write_unit(uint64_t unit, const uint8_t in[kUnit]);
  lib::fs::Io* part_ = nullptr;
  const lib::crypto::Xts256* key_ = nullptr;
  uint64_t units_ = 0;
  uint64_t limit_ = 0;
};

// Зашифровать единицы [from, to) на месте (они должны быть открытыми).
lib::fs::Status encrypt_units(lib::fs::Io* part, const lib::crypto::Xts256& key, uint64_t from, uint64_t to);

}  // namespace lib::sysarea
