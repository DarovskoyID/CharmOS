#pragma once
// Паника: сообщение, регистры (если есть кадр прерывания), обратная трассировка
// по цепочке RBP, затем остановка. В QEMU — выход с kExitFailure.
// Адреса трассировки переводятся в строки исходника офлайн:
//   llvm-addr2line -fpe build/kernel/kernel/kernel.elf <адрес>...

#include <arch/x86_64/idt.hpp>

#include "log.hpp"

namespace kernel {

[[noreturn]] void halt_with_failure();

// Печатает кадр прерывания и трассировку стека от его RBP.
void dump_frame(const arch::x86_64::InterruptFrame& frame);

// Трассировка от заданного RBP.
void dump_backtrace(uint64_t rbp);

template <typename... Args>
[[noreturn]] void panic(lib::core::StringView fmt, const Args&... args) {
  asm volatile("cli");
  kprint("\nKERNEL PANIC: ");
  kprint(fmt, args...);
  kprint("\n");
  uint64_t rbp;
  asm volatile("mov %%rbp, %0" : "=r"(rbp));
  dump_backtrace(rbp);
  halt_with_failure();
}

// Обработчик векторов без своего обработчика: исключение → паника.
void unhandled_interrupt(arch::x86_64::InterruptFrame& frame);

}  // namespace kernel
