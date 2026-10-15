#pragma once
// SHA-512 (FIPS PUB 180-4, §5.3.5, §6.4). Без зависимостей: для ядра,
// юзерспейса и хоста (tools/sign).

#include <stddef.h>
#include <stdint.h>

namespace lib::crypto {

inline constexpr size_t kSha512Size = 64;

class Sha512 {
 public:
  Sha512() { reset(); }
  void reset();
  void update(const void* data, size_t len);
  void finish(uint8_t out[kSha512Size]);

 private:
  void block(const uint8_t* p);
  uint64_t h_[8];
  uint8_t buf_[128];
  size_t used_;
  uint64_t total_;  // байт (сообщения до 2^61 байт — нам хватит)
};

void sha512(const void* data, size_t len, uint8_t out[kSha512Size]);

}  // namespace lib::crypto
