#pragma once

#include <stddef.h>
#include <stdint.h>

#include <arch/x86_64/io.hpp>

namespace arch::x86_64 {

// 16550 UART на COM1. Не требует ничего, кроме портов ввода-вывода:
// работает с первой инструкции, до памяти и прерываний.
class Serial {
 public:
  static constexpr uint16_t kCom1 = 0x3f8;

  static void init() {
    outb(kCom1 + 1, 0x00);  // прерывания выкл.
    outb(kCom1 + 3, 0x80);  // DLAB
    outb(kCom1 + 0, 0x01);  // делитель 1 → 115200
    outb(kCom1 + 1, 0x00);
    outb(kCom1 + 3, 0x03);  // 8N1
    outb(kCom1 + 2, 0xc7);  // FIFO вкл., сброс
    outb(kCom1 + 4, 0x03);  // DTR + RTS
  }

  static void put(char c) {
    while ((inb(kCom1 + 5) & 0x20) == 0) {
    }
    outb(kCom1, static_cast<uint8_t>(c));
  }

  static void write(const char* s, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      if (s[i] == '\n') put('\r');
      put(s[i]);
    }
  }

  // Приём (docs/specs/ns16c2752.pdf, совместимая с 16550 раскладка):
  // LSR (+5) бит 0 — Data Ready (табл. LSR), RBR (+0) — принятый байт;
  // IER (+1) бит 0 — прерывание «есть данные» (табл. 5); MCR (+4) бит 3 —
  // OUT2 (на PC им разрешается выход прерывания UART).
  static bool rx_ready() { return (inb(kCom1 + 5) & 0x01) != 0; }
  static uint8_t get() { return inb(kCom1); }
  static void enable_rx_interrupt() {
    outb(kCom1 + 4, 0x0b);  // DTR + RTS + OUT2
    outb(kCom1 + 1, 0x01);
  }

  static void write(const char* s) {
    while (*s != '\0') {
      if (*s == '\n') put('\r');
      put(*s++);
    }
  }
};

// Протокол выхода тестов: QEMU -device isa-debug-exit,iobase=0xf4,iosize=0x04.
// QEMU завершается с кодом (code << 1) | 1.
inline constexpr uint16_t kDebugExitPort = 0xf4;
inline constexpr uint32_t kExitSuccess = 0x10;  // → код процесса 33
inline constexpr uint32_t kExitFailure = 0x11;  // → код процесса 35

[[noreturn]] inline void qemu_exit(uint32_t code) {
  outl(kDebugExitPort, code);
  halt_forever();  // на реальном железе порта нет
}

}  // namespace arch::x86_64
