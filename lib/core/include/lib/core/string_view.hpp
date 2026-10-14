#pragma once
// Невладеющая строка: указатель + длина, без завершающего нуля.

#include <stddef.h>

namespace lib::core {

class StringView {
 public:
  constexpr StringView() = default;
  constexpr StringView(const char* data, size_t size) : data_(data), size_(size) {}
  // Из C-строки (обязательно с завершающим нулём).
  constexpr StringView(const char* cstr) : data_(cstr), size_(length(cstr)) {}  // NOLINT

  constexpr const char* data() const { return data_; }
  constexpr size_t size() const { return size_; }
  constexpr bool empty() const { return size_ == 0; }
  constexpr char operator[](size_t i) const { return data_[i]; }

  constexpr bool operator==(StringView other) const {
    if (size_ != other.size_) return false;
    for (size_t i = 0; i < size_; ++i) {
      if (data_[i] != other.data_[i]) return false;
    }
    return true;
  }

  constexpr bool starts_with(StringView prefix) const {
    return prefix.size_ <= size_ && StringView(data_, prefix.size_) == prefix;
  }

  constexpr StringView substr(size_t pos, size_t count = ~size_t{0}) const {
    if (pos > size_) pos = size_;
    size_t rest = size_ - pos;
    return StringView(data_ + pos, count < rest ? count : rest);
  }

 private:
  static constexpr size_t length(const char* s) {
    size_t n = 0;
    while (s[n] != '\0') ++n;
    return n;
  }

  const char* data_ = "";
  size_t size_ = 0;
};

}  // namespace lib::core
