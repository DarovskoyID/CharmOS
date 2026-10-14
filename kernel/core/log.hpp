#pragma once
// Журнал ядра. Пока единственный приёмник — COM1 (работает с первой
// инструкции ядра, см. docs/abi/boot.md §2). Каждый вызов kprint выводится
// целиком, строки разных CPU не перемешиваются.

#include <lib/core/fmt.hpp>

#include "../sync/spinlock.hpp"

namespace kernel {

lib::core::Sink& log_sink();
sync::Spinlock& log_lock();

template <typename... Args>
void kprint(lib::core::StringView fmt, const Args&... args) {
  sync::SpinGuard guard(log_lock());
  lib::core::format(log_sink(), fmt, args...);
}

// Без блокировки — только для паники: блокировку журнала может держать
// упавший код, а дождаться её уже нельзя.
template <typename... Args>
void kprint_unlocked(lib::core::StringView fmt, const Args&... args) {
  lib::core::format(log_sink(), fmt, args...);
}

}  // namespace kernel
