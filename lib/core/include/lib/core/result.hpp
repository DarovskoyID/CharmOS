#pragma once
// Result<T, E>: значение или ошибка. Ядро не использует исключения, поэтому
// всё, что может не получиться (в первую очередь выделение памяти), возвращает
// Result (правило 6 CLAUDE.md). [[nodiscard]] — результат нельзя забыть.
//
// T и E обязаны быть тривиально копируемыми: в ядре нет <new> и деструкторов
// с побочными эффектами в таких типах.

namespace lib::core {

struct Unit {};

template <typename T, typename E>
class [[nodiscard]] Result {
  static_assert(__is_trivially_copyable(T), "Result<T>: T must be trivially copyable");
  static_assert(__is_trivially_copyable(E), "Result<T>: E must be trivially copyable");

 public:
  static constexpr Result ok(T value) {
    Result r;
    r.ok_ = true;
    r.value_ = value;
    return r;
  }
  static constexpr Result err(E error) {
    Result r;
    r.ok_ = false;
    r.error_ = error;
    return r;
  }

  constexpr bool is_ok() const { return ok_; }
  constexpr bool is_err() const { return !ok_; }

  // Доступ к не тому варианту — ошибка программиста: аварийная остановка.
  constexpr T value() const {
    if (!ok_) __builtin_trap();
    return value_;
  }
  constexpr E error() const {
    if (ok_) __builtin_trap();
    return error_;
  }
  constexpr T value_or(T fallback) const { return ok_ ? value_ : fallback; }

 private:
  constexpr Result() : error_{} {}

  bool ok_;
  union {
    T value_;
    E error_;
  };
};

// Результат без значения.
template <typename E>
using Status = Result<Unit, E>;

template <typename E>
constexpr Status<E> ok_status() {
  return Status<E>::ok(Unit{});
}

}  // namespace lib::core
