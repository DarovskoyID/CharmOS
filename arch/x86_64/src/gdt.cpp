#include <arch/x86_64/gdt.hpp>
#include <stddef.h>

namespace arch::x86_64 {

namespace {

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

struct [[gnu::packed]] Gdtr {
  uint16_t limit;
  uint64_t base;
};

constexpr size_t kIstStackSize = 16 * 1024;

alignas(16) constinit uint8_t g_ist_stacks[3][kIstStackSize];
alignas(16) constinit Tss g_tss = {};

// Дескрипторы сегментов (SDM §3.4.5): база и предел в long mode игнорируются,
// значимы P, DPL, S, тип, L (бит 53) и D/B.
alignas(16) constinit uint64_t g_gdt[7] = {
    0,
    0x00af9a000000ffffull,  // 0x08: код ядра, DPL 0, L=1
    0x00cf92000000ffffull,  // 0x10: данные ядра, DPL 0
    0x00cff2000000ffffull,  // 0x18: данные пользователя, DPL 3
    0x00affa000000ffffull,  // 0x20: код пользователя, DPL 3, L=1
    0,                      // 0x28: TSS, младшая половина (заполняется в init)
    0,                      //       TSS, старшая половина
};

// SDM рис. 8-4: системный дескриптор TSS в long mode занимает 16 байт.
void set_tss_descriptor(uint64_t base, uint32_t limit) {
  uint64_t lo = 0;
  lo |= limit & 0xffff;
  lo |= (base & 0xffffff) << 16;
  lo |= uint64_t{0x89} << 40;  // P=1, DPL=0, тип 9 = доступный 64-битный TSS
  lo |= uint64_t{(limit >> 16) & 0xf} << 48;
  lo |= ((base >> 24) & 0xff) << 56;
  g_gdt[5] = lo;
  g_gdt[6] = base >> 32;
}

}  // namespace

void init_gdt() {
  for (int i = 0; i < 3; ++i) {
    g_tss.ist[i] = reinterpret_cast<uint64_t>(&g_ist_stacks[i][kIstStackSize]);
  }
  g_tss.iomap_base = sizeof(Tss);  // карты ввода-вывода нет
  set_tss_descriptor(reinterpret_cast<uint64_t>(&g_tss), sizeof(Tss) - 1);

  Gdtr gdtr = {sizeof g_gdt - 1, reinterpret_cast<uint64_t>(&g_gdt)};
  asm volatile(
      "lgdt %0\n"
      "pushq %1\n"
      "leaq 1f(%%rip), %%rax\n"
      "pushq %%rax\n"
      "lretq\n"
      "1:\n"
      "mov %2, %%ds\n"
      "mov %2, %%es\n"
      "mov %2, %%ss\n"
      "xor %%eax, %%eax\n"
      "mov %%eax, %%fs\n"
      "mov %%eax, %%gs\n"
      "ltr %3\n"
      :
      : "m"(gdtr), "i"(kKernelCode), "r"(static_cast<uint32_t>(kKernelData)),
        "r"(kTssSelector)
      : "rax", "memory");
}

}  // namespace arch::x86_64
