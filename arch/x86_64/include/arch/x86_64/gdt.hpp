#pragma once
// GDT и 64-битный TSS ядра (docs/specs/intel-sdm-vol3a.pdf §3.4.5, §8.2.3, §8.7).
//
// Селекторы (порядок user data → user code нужен для SYSRET в M3):
//   0x08 — код ядра (64-бит), 0x10 — данные ядра,
//   0x18 — данные пользователя, 0x20 — код пользователя, 0x28 — TSS (16 байт).
// Стеки IST: 1 — #DF, 2 — NMI, 3 — #MC. Отдельный стек гарантирует, что
// обработчик запустится даже при испорченном стеке ядра.

#include <stdint.h>

namespace arch::x86_64 {

inline constexpr uint16_t kKernelCode = 0x08;
inline constexpr uint16_t kKernelData = 0x10;
inline constexpr uint16_t kUserData = 0x18;
inline constexpr uint16_t kUserCode = 0x20;
inline constexpr uint16_t kTssSelector = 0x28;

inline constexpr uint8_t kIstDoubleFault = 1;
inline constexpr uint8_t kIstNmi = 2;
inline constexpr uint8_t kIstMachineCheck = 3;

// Загружает GDT ядра, перезагружает сегментные регистры и TR.
// Пока один процессор (BSP); per-CPU GDT/TSS — в M3.
void init_gdt();

}  // namespace arch::x86_64
