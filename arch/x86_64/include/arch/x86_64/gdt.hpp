#pragma once
// GDT и 64-битный TSS (docs/specs/intel-sdm-vol3a.pdf §3.4.5, §8.2.3, §8.7).
// У каждого процессора свои GDT и TSS (TSS не может быть общим: процессор
// помечает его занятым при LTR).
//
// Селекторы (порядок user data → user code нужен для SYSRET):
//   0x08 — код ядра (64-бит), 0x10 — данные ядра,
//   0x18 — данные пользователя, 0x20 — код пользователя, 0x28 — TSS (16 байт).
// Стеки IST: 1 — #DF, 2 — NMI, 3 — #MC. Отдельный стек гарантирует, что
// обработчик запустится даже при испорченном стеке ядра.

#include <stddef.h>
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
inline constexpr int kIstCount = 3;

// SDM Vol. 3A, рис. 8-11: 64-битный TSS. Поле rsp0 лежит по смещению 4,
// поэтому структура упакована.
struct [[gnu::packed]] Tss {
  uint32_t reserved0;
  uint64_t rsp[3];
  uint64_t reserved1;
  uint64_t ist[7];
  uint64_t reserved2;
  uint16_t reserved3;
  uint16_t iomap_base;
};
static_assert(sizeof(Tss) == 104);
static_assert(offsetof(Tss, rsp) == 4);
static_assert(offsetof(Tss, ist) == 36);
static_assert(offsetof(Tss, iomap_base) == 102);

struct CpuDescriptors {
  alignas(16) uint64_t gdt[7];
  alignas(16) Tss tss;
};

// Заполняет GDT и TSS процессора и загружает их на текущем CPU:
// LGDT, перезагрузка сегментных регистров, LTR. ist_tops — вершины стеков IST1..3.
void load_gdt(CpuDescriptors& d, const uint64_t (&ist_tops)[kIstCount]);

// Стек ядра для входа из ring 3 (TSS.RSP0).
inline void set_kernel_entry_stack(CpuDescriptors& d, uint64_t rsp0) { d.tss.rsp[0] = rsp0; }

}  // namespace arch::x86_64
