#pragma once

#include <stdint.h>

namespace arch::x86_64 {

inline void outb(uint16_t port, uint8_t value) {
  asm volatile("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

inline uint8_t inb(uint16_t port) {
  uint8_t value;
  asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
  return value;
}

inline void outl(uint16_t port, uint32_t value) {
  asm volatile("outl %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

[[noreturn]] inline void halt_forever() {
  for (;;) asm volatile("cli; hlt");
}

}  // namespace arch::x86_64
