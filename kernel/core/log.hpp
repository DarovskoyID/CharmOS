#pragma once
// Журнал ядра. Пока единственный приёмник — COM1 (работает с первой
// инструкции ядра, см. docs/abi/boot.md §2).

#include <lib/core/fmt.hpp>

namespace kernel {

lib::core::Sink& log_sink();

template <typename... Args>
void kprint(lib::core::StringView fmt, const Args&... args) {
  lib::core::format(log_sink(), fmt, args...);
}

}  // namespace kernel
