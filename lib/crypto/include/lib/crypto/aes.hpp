#pragma once
// AES-256 (FIPS 197, §5) и XTS-AES-256 (IEEE P1619/D16 §5, NIST SP 800-38E).
// Без зависимостей: для ядра (шифрование закрытой области) и хост-тестов.
// Реализация табличная (раунд — поиск по 32-битным таблицам): время зависит
// от данных (кэш), это защита от кражи диска, а не от соседа по процессору.

#include <stddef.h>
#include <stdint.h>

namespace lib::crypto {

inline constexpr size_t kAesBlock = 16;
inline constexpr size_t kAes256Key = 32;

class Aes256 {
 public:
  void set_key(const uint8_t key[kAes256Key]);
  void encrypt(const uint8_t in[kAesBlock], uint8_t out[kAesBlock]) const;
  void decrypt(const uint8_t in[kAesBlock], uint8_t out[kAesBlock]) const;
  void wipe();

 private:
  static constexpr int kRounds = 14;           // Nr для Nk = 8 (§5, рис. 4)
  uint8_t w_[(kRounds + 1) * kAesBlock] = {};  // расписание ключей (§5.2)
  uint8_t dw_[(kRounds + 1) * kAesBlock] = {};  // для EQINVCIPHER (§5.3.5)
};

// XTS-AES-256: ключ 64 байта = Key1 (данные) | Key2 (твик). Единица данных —
// кратна 16 байтам (без кражи шифротекста: нам нужны только сектора).
class Xts256 {
 public:
  // false — Key1 == Key2 (SP 800-38E требует их различия).
  bool set_key(const uint8_t key[2 * kAes256Key]);
  // unit — номер единицы данных (P1619 §5.1: твик — little-endian 128 бит).
  bool encrypt(uint64_t unit, const uint8_t* in, uint8_t* out, size_t len) const;
  bool decrypt(uint64_t unit, const uint8_t* in, uint8_t* out, size_t len) const;
  void wipe();

 private:
  bool run(uint64_t unit, const uint8_t* in, uint8_t* out, size_t len, bool enc) const;
  Aes256 data_, tweak_;
};

}  // namespace lib::crypto
