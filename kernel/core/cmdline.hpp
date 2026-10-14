#pragma once
// Командная строка ядра: «ключ=значение» и «флаг» через пробелы.

#include <lib/core/string_view.hpp>

namespace kernel {

class Cmdline {
 public:
  constexpr Cmdline() = default;
  explicit constexpr Cmdline(lib::core::StringView text) : text_(text) {}

  // Значение ключа или пустая строка. found = true, если ключ (или флаг) есть.
  lib::core::StringView get(lib::core::StringView key, bool* found = nullptr) const;
  bool has(lib::core::StringView key) const;

  lib::core::StringView text() const { return text_; }

 private:
  lib::core::StringView text_;
};

}  // namespace kernel
