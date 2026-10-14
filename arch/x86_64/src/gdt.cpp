#include <arch/x86_64/gdt.hpp>

namespace arch::x86_64 {

namespace {

struct [[gnu::packed]] Gdtr {
  uint16_t limit;
  uint64_t base;
};

// Дескрипторы сегментов (SDM §3.4.5): база и предел в long mode игнорируются,
// значимы P, DPL, S, тип, L (бит 53) и D/B.
constexpr uint64_t kGdtTemplate[5] = {
    0,
    0x00af9a000000ffffull,  // 0x08: код ядра, DPL 0, L=1
    0x00cf92000000ffffull,  // 0x10: данные ядра, DPL 0
    0x00cff2000000ffffull,  // 0x18: данные пользователя, DPL 3
    0x00affa000000ffffull,  // 0x20: код пользователя, DPL 3, L=1
};

// SDM рис. 8-4: системный дескриптор TSS в long mode занимает 16 байт.
void set_tss_descriptor(uint64_t* gdt, uint64_t base, uint32_t limit) {
  uint64_t lo = 0;
  lo |= limit & 0xffff;
  lo |= (base & 0xffffff) << 16;
  lo |= uint64_t{0x89} << 40;  // P=1, DPL=0, тип 9 = доступный 64-битный TSS
  lo |= uint64_t{(limit >> 16) & 0xf} << 48;
  lo |= ((base >> 24) & 0xff) << 56;
  gdt[5] = lo;
  gdt[6] = base >> 32;
}

}  // namespace

void load_gdt(CpuDescriptors& d, const uint64_t (&ist_tops)[kIstCount]) {
  for (int i = 0; i < 5; ++i) d.gdt[i] = kGdtTemplate[i];
  d.tss = {};
  for (int i = 0; i < kIstCount; ++i) d.tss.ist[i] = ist_tops[i];
  d.tss.iomap_base = sizeof(Tss);  // карты ввода-вывода нет
  set_tss_descriptor(d.gdt, reinterpret_cast<uint64_t>(&d.tss), sizeof(Tss) - 1);

  Gdtr gdtr = {sizeof d.gdt - 1, reinterpret_cast<uint64_t>(&d.gdt)};
  // FS/GS обнуляются, но их базы (MSR) не трогаем: GS base держит per-CPU данные.
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
      "ltr %3\n"
      :
      : "m"(gdtr), "i"(kKernelCode), "r"(static_cast<uint32_t>(kKernelData)),
        "r"(kTssSelector)
      : "rax", "memory");
}

}  // namespace arch::x86_64
