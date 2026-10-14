#pragma once
// Форматированный вывод без аллокаций и без printf.
//
//   format(sink, "addr={x} size={} name={}", addr, size, "kernel");
//
// Заполнители:
//   {}   — целое в десятичном виде, строка, bool, указатель (как {x})
//   {x}  — целое/указатель в шестнадцатеричном виде: 0x + минимум цифр
//   {X}  — то же, но ровно 16 цифр (удобно для адресов и регистров)
//   {{ и }} — литеральные скобки
// Лишний заполнитель печатается как <?>, лишние аргументы игнорируются.

#include <stddef.h>
#include <stdint.h>

#include <lib/core/string_view.hpp>

namespace lib::core {

class Sink {
 public:
  virtual void write(const char* data, size_t size) = 0;

 protected:
  ~Sink() = default;
};

// Пишет в фиксированный буфер; лишнее отбрасывает, всегда завершает нулём.
class BufferSink final : public Sink {
 public:
  BufferSink(char* buf, size_t cap) : buf_(buf), cap_(cap) {
    if (cap_ > 0) buf_[0] = '\0';
  }
  void write(const char* data, size_t size) override;
  StringView view() const { return StringView(buf_, len_); }
  bool truncated() const { return truncated_; }

 private:
  char* buf_;
  size_t cap_;
  size_t len_ = 0;
  bool truncated_ = false;
};

class FmtArg {
 public:
  enum class Kind : uint8_t { None, Unsigned, Signed, Bool, Char, String, Pointer };

  constexpr FmtArg() = default;

  template <typename T>
  constexpr FmtArg(T v) {  // NOLINT: неявное преобразование задумано
    if constexpr (__is_same(T, bool)) {
      kind_ = Kind::Bool;
      u_ = v ? 1 : 0;
    } else if constexpr (__is_same(T, char)) {
      kind_ = Kind::Char;
      u_ = static_cast<unsigned char>(v);
    } else if constexpr (__is_enum(T)) {
      kind_ = Kind::Unsigned;
      u_ = static_cast<uint64_t>(v);
    } else if constexpr (__is_integral(T) && __is_signed(T)) {
      kind_ = Kind::Signed;
      s_ = static_cast<int64_t>(v);
    } else if constexpr (__is_integral(T)) {
      kind_ = Kind::Unsigned;
      u_ = static_cast<uint64_t>(v);
    } else if constexpr (__is_same(T, const char*) || __is_same(T, char*)) {
      kind_ = Kind::String;
      str_ = v != nullptr ? StringView(v) : StringView("(null)");
    } else if constexpr (__is_same(T, StringView)) {
      kind_ = Kind::String;
      str_ = v;
    } else if constexpr (__is_pointer(T)) {
      kind_ = Kind::Pointer;
      u_ = reinterpret_cast<uint64_t>(v);
    } else {
      static_assert(sizeof(T) == 0, "fmt: unsupported argument type");
    }
  }

  // Строковые литералы приходят массивами.
  template <size_t N>
  constexpr FmtArg(const char (&s)[N]) : kind_(Kind::String), str_(s, N - 1) {}

  Kind kind() const { return kind_; }
  uint64_t as_unsigned() const { return u_; }
  int64_t as_signed() const { return s_; }
  StringView as_string() const { return str_; }

 private:
  Kind kind_ = Kind::None;
  union {
    uint64_t u_ = 0;
    int64_t s_;
  };
  StringView str_;
};

void vformat(Sink& sink, StringView fmt, const FmtArg* args, size_t count);

template <typename... Args>
void format(Sink& sink, StringView fmt, const Args&... args) {
  const FmtArg list[] = {FmtArg(args)..., FmtArg()};
  vformat(sink, fmt, list, sizeof...(Args));
}

// Удобная обёртка: форматирует в буфер, возвращает получившуюся строку.
template <typename... Args>
StringView format_to(char* buf, size_t cap, StringView fmt, const Args&... args) {
  BufferSink sink(buf, cap);
  format(sink, fmt, args...);
  return sink.view();
}

}  // namespace lib::core
