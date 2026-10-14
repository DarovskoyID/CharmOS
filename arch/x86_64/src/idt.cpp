#include <arch/x86_64/gdt.hpp>
#include <arch/x86_64/idt.hpp>
#include <stddef.h>

extern "C" const uint64_t isr_stub_table[256];

namespace arch::x86_64 {

namespace {

// SDM Vol. 3A рис. 6-8: 64-битный шлюз прерывания, 16 байт.
struct IdtGate {
  uint16_t offset_low;
  uint16_t selector;
  uint8_t ist;        // биты 2:0
  uint8_t type_attr;  // P | DPL | 0 | тип
  uint16_t offset_mid;
  uint32_t offset_high;
  uint32_t reserved;
};
static_assert(sizeof(IdtGate) == 16);
static_assert(offsetof(IdtGate, offset_mid) == 6);

struct [[gnu::packed]] Idtr {
  uint16_t limit;
  uint64_t base;
};

constexpr uint8_t kInterruptGate = 0x8e;  // P=1, DPL=0, тип 0xE (IF сбрасывается)

alignas(16) constinit IdtGate g_idt[256] = {};
constinit InterruptHandler g_handlers[256] = {};
constinit InterruptHandler g_fallback = nullptr;

void set_gate(uint8_t vector, uint64_t handler, uint8_t ist) {
  IdtGate& g = g_idt[vector];
  g.offset_low = static_cast<uint16_t>(handler);
  g.selector = kKernelCode;
  g.ist = ist;
  g.type_attr = kInterruptGate;
  g.offset_mid = static_cast<uint16_t>(handler >> 16);
  g.offset_high = static_cast<uint32_t>(handler >> 32);
  g.reserved = 0;
}

constexpr const char* kExceptionNames[32] = {
    "#DE divide error",         "#DB debug",
    "NMI",                      "#BP breakpoint",
    "#OF overflow",             "#BR bound range",
    "#UD invalid opcode",       "#NM device not available",
    "#DF double fault",         "coprocessor segment overrun",
    "#TS invalid TSS",          "#NP segment not present",
    "#SS stack-segment fault",  "#GP general protection",
    "#PF page fault",           "reserved (15)",
    "#MF x87 floating-point",   "#AC alignment check",
    "#MC machine check",        "#XM SIMD floating-point",
    "#VE virtualization",       "#CP control protection",
    "reserved (22)",            "reserved (23)",
    "reserved (24)",            "reserved (25)",
    "reserved (26)",            "reserved (27)",
    "reserved (28)",            "reserved (29)",
    "reserved (30)",            "reserved (31)",
};

}  // namespace

const char* exception_name(uint64_t vector) {
  return vector < 32 ? kExceptionNames[vector] : "external interrupt";
}

void init_idt() {
  for (int v = 0; v < 256; ++v) {
    uint8_t ist = 0;
    if (v == kVectorDoubleFault) ist = kIstDoubleFault;
    if (v == kVectorNmi) ist = kIstNmi;
    if (v == kVectorMachineCheck) ist = kIstMachineCheck;
    set_gate(static_cast<uint8_t>(v), isr_stub_table[v], ist);
  }
  Idtr idtr = {sizeof g_idt - 1, reinterpret_cast<uint64_t>(&g_idt)};
  asm volatile("lidt %0" : : "m"(idtr) : "memory");
}

void set_interrupt_handler(uint8_t vector, InterruptHandler handler) {
  g_handlers[vector] = handler;
}

void set_fallback_handler(InterruptHandler handler) { g_fallback = handler; }

}  // namespace arch::x86_64

extern "C" void interrupt_dispatch(arch::x86_64::InterruptFrame* frame) {
  using namespace arch::x86_64;
  InterruptHandler h = g_handlers[frame->vector & 0xff];
  if (h == nullptr) h = g_fallback;
  if (h != nullptr) {
    h(*frame);
    return;
  }
  // Ни обработчика, ни запасного: дальше идти некуда.
  for (;;) asm volatile("cli; hlt");
}
