#pragma once
// Манифест драйвера (docs/abi/driver.md §7): строгое подмножество TOML.
// Разбирается в ядре, поэтому всё, чего нет в документе, — ошибка, а не
// «как-нибудь»: неизвестный ключ, повтор ключа, число вне диапазона, мусор
// после значения.

#include <stddef.h>
#include <stdint.h>

#include <lib/core/result.hpp>
#include <lib/core/string_view.hpp>

namespace lib::manifest {

inline constexpr size_t kMaxName = 32;
inline constexpr size_t kMaxMatches = 8;
inline constexpr size_t kMaxDevices = 8;
inline constexpr uint32_t kMaxScore = 65535;

struct Match {
  bool has_vendor;
  uint16_t vendor;
  uint32_t device_count;  // 0 — любое устройство
  uint16_t devices[kMaxDevices];
  bool has_class;
  uint32_t class_code;  // 24 бита
  uint32_t score;
};

struct Manifest {
  char name[kMaxName];  // с завершающим 0
  uint32_t match_count;
  Match matches[kMaxMatches];
};

struct ParseError {
  uint32_t line;        // с 1
  const char* message;  // статическая строка
};

lib::core::Result<Manifest, ParseError> parse(lib::core::StringView text);

struct DeviceId {
  uint16_t vendor;
  uint16_t device;
  uint32_t class_code;
};

// Наибольший score среди совпавших [[match]] или -1, если ни один не совпал.
int64_t match_score(const Manifest& m, const DeviceId& id);

}  // namespace lib::manifest
