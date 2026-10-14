#pragma once
// Встроенные тесты ядра, выбираются командной строкой: ktest=<имя>.
// Сценарии QEMU (tests/qemu/m2/*) задают имя через <сценарий>.cmdline.
// Успех — строка «ktest: PASS <имя>» и выход QEMU с кодом 33.

#include <arch/x86_64/idt.hpp>
#include <lib/core/string_view.hpp>

#include "cmdline.hpp"

namespace kernel::ktest {

// Запоминает выбранный тест. Вызывать до первого исключения.
void select(const Cmdline& cmdline);
lib::core::StringView selected();

// Тесты, которые вызывают исключение, ожидают его здесь.
bool exception_expected(const arch::x86_64::InterruptFrame& frame);

// Тесты, проверяющие, что ядро паникует (нарушение порядка блокировок и т.п.).
bool panic_expected();

// Запускает выбранный тест, если он есть. Возвращается, только если тест
// не выбран (обычная загрузка).
void run_selected();

[[noreturn]] void pass();
[[noreturn]] void fail(lib::core::StringView why);

}  // namespace kernel::ktest
