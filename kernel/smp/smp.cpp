#include "smp.hpp"

#include <arch/x86_64/cpu.hpp>
#include <arch/x86_64/idt.hpp>
#include <lib/core/string_view.hpp>

#include "../core/panic.hpp"
#include "../mm/layout.hpp"
#include "../mm/pmm.hpp"
#include "../mm/stack.hpp"
#include "../mm/vmm.hpp"
#include "../platform/platform.hpp"
#include "../proc/syscall.hpp"
#include "../sched/sched.hpp"

extern "C" const char ap_trampoline_start[], ap_trampoline_end[];
extern "C" const char ap_tramp_gdtr[], ap_tramp_gdt[], ap_tramp_far32[], ap_tramp_far64[];
extern "C" const char ap_tramp_pm32[], ap_tramp_lm64[];
extern "C" const char ap_tramp_cr3[], ap_tramp_stack[], ap_tramp_entry[], ap_tramp_arg[];

extern "C" [[noreturn]] void ap_entry(kernel::smp::Cpu* cpu);

namespace kernel::smp {

namespace {

using arch::x86_64::kIstCount;

constinit Cpu g_cpus[kMaxCpus] = {};
constinit volatile uint32_t g_count = 0;
constinit bool g_percpu = false;

// Стеки IST BSP до появления аллокатора стеков (заменяются в late_init_bsp).
constexpr size_t kEarlyIstSize = 8 * 1024;
alignas(16) constinit uint8_t g_early_ist[kIstCount][kEarlyIstSize];

// IA32_KERNEL_GS_BASE (SDM Vol. 2B, SWAPGS): после swapgs становится базой GS.
constexpr uint32_t kMsrKernelGsBase = 0xc0000102;

void install_gs(Cpu* c) {
  arch::x86_64::wrmsr(kMsrKernelGsBase, reinterpret_cast<uint64_t>(c));
  asm volatile("swapgs" ::: "memory");
}

uint64_t offset_of(const char* sym) {
  return static_cast<uint64_t>(sym - ap_trampoline_start);
}

template <typename T>
void poke(uint64_t page_phys, const char* sym, T value) {
  *mm::phys_to_virt<T>(page_phys + offset_of(sym)) = value;
}

void alloc_ist_stacks(Cpu& c) {
  for (int i = 0; i < kIstCount; ++i) {
    auto s = mm::alloc_kernel_stack(16 * 1024);
    if (s.is_err()) panic("smp: no memory for IST stack");
    c.ist_tops[i] = s.value().top;
    c.descriptors.tss.ist[i] = s.value().top;  // TSS читается при исключении
  }
}

void wakeup_ipi_handler(arch::x86_64::InterruptFrame&) { platform::local_apic().eoi(); }

}  // namespace

bool percpu_ready() { return g_percpu; }
uint32_t cpu_count() { return g_count; }
Cpu& cpu(uint32_t index) { return g_cpus[index]; }

void early_init_bsp() {
  Cpu& c = g_cpus[0];
  c.self = &c;
  c.index = 0;
  for (int i = 0; i < kIstCount; ++i) {
    c.ist_tops[i] = reinterpret_cast<uint64_t>(&g_early_ist[i][kEarlyIstSize]);
  }
  arch::x86_64::load_gdt(c.descriptors, c.ist_tops);
  arch::x86_64::init_idt();
  install_gs(&c);
  g_percpu = true;
  c.online = true;
  g_count = 1;
}

void late_init_bsp() {
  Cpu& c = g_cpus[0];
  alloc_ist_stacks(c);
  c.apic_id = platform::local_apic().id();
  arch::x86_64::set_interrupt_handler(platform::kVectorWakeup, &wakeup_ipi_handler);
  proc::init_syscall_this_cpu();
}

void start_aps() {
  const auto& madt = platform::madt();
  const auto& hpet = platform::hpet();
  auto& lapic = platform::local_apic();

  // 4 страницы ниже 1 МиБ: код+данные, PML4, PDPT, PD.
  auto frames = mm::alloc_frames_below(1 << 20, 4);
  if (frames.is_err()) panic("smp: no memory below 1 MiB for AP trampoline");
  uint64_t t = frames.value();
  if (t > 0xff000) panic("smp: trampoline page {x} not addressable by SIPI", t);

  size_t blob = static_cast<size_t>(ap_trampoline_end - ap_trampoline_start);
  if (blob > 4096) panic("smp: trampoline does not fit in a page");
  auto* page = mm::phys_to_virt<uint8_t>(t);
  for (size_t i = 0; i < blob; ++i) page[i] = static_cast<uint8_t>(ap_trampoline_start[i]);

  // Таблицы трамплина: тождественные первые 2 МиБ + верхняя половина ядра.
  constexpr uint64_t kP = 1, kRw = 2, kPs = 1 << 7;
  auto* pml4 = mm::phys_to_virt<uint64_t>(t + 0x1000);
  auto* pdpt = mm::phys_to_virt<uint64_t>(t + 0x2000);
  auto* pd = mm::phys_to_virt<uint64_t>(t + 0x3000);
  for (int i = 0; i < 512; ++i) pml4[i] = pdpt[i] = pd[i] = 0;
  pml4[0] = (t + 0x2000) | kP | kRw;
  pdpt[0] = (t + 0x3000) | kP | kRw;
  pd[0] = 0 | kP | kRw | kPs;
  const auto* kernel_pml4 = mm::phys_to_virt<const uint64_t>(mm::kernel_root());
  for (int i = 256; i < 512; ++i) pml4[i] = kernel_pml4[i];

  poke<uint32_t>(t, ap_tramp_gdtr + 2, static_cast<uint32_t>(t + offset_of(ap_tramp_gdt)));
  poke<uint32_t>(t, ap_tramp_far32, static_cast<uint32_t>(t + offset_of(ap_tramp_pm32)));
  poke<uint32_t>(t, ap_tramp_far64, static_cast<uint32_t>(t + offset_of(ap_tramp_lm64)));
  poke<uint64_t>(t, ap_tramp_cr3, t + 0x1000);
  poke<uint64_t>(t, ap_tramp_entry, reinterpret_cast<uint64_t>(&ap_entry));

  uint32_t bsp_apic = g_cpus[0].apic_id;
  for (size_t i = 0; i < madt.cpu_count; ++i) {
    const auto& info = madt.cpus[i];
    if (!info.enabled || info.apic_id == bsp_apic) continue;
    if (g_count == kMaxCpus) {
      kprint("smp: more than {} CPUs, ignoring the rest\n", kMaxCpus);
      break;
    }
    if (lapic.mode() == arch::x86_64::LocalApic::Mode::XApic && info.apic_id > 0xfe) {
      kprint("smp: apic {} needs x2APIC, skipped\n", info.apic_id);
      continue;
    }
    uint32_t idx = g_count;
    Cpu& c = g_cpus[idx];
    c.self = &c;
    c.index = idx;
    c.apic_id = info.apic_id;
    c.online = false;
    alloc_ist_stacks(c);
    auto boot_stack = mm::alloc_kernel_stack();
    if (boot_stack.is_err()) panic("smp: no memory for AP stack");
    poke<uint64_t>(t, ap_tramp_stack, boot_stack.value().top);
    poke<uint64_t>(t, ap_tramp_arg, reinterpret_cast<uint64_t>(&c));

    // SDM табл. 9-1: INIT, 10 мс, SIPI, 200 мкс, SIPI.
    lapic.send_init(c.apic_id);
    hpet.wait_ns(10'000'000);
    auto vector = static_cast<uint8_t>(t >> 12);
    lapic.send_startup(c.apic_id, vector);
    hpet.wait_ns(200'000);
    if (!c.online) lapic.send_startup(c.apic_id, vector);

    uint64_t start = hpet.counter();
    while (!c.online && hpet.elapsed(start, hpet.counter()) < hpet.ns_to_ticks(1'000'000'000)) {
      arch::x86_64::pause();
    }
    if (!c.online) {
      kprint("smp: cpu with apic {} did not start\n", c.apic_id);
      continue;
    }
    g_count = idx + 1;
  }
  mm::free_frames(t, 4);
  kprint("smp: {} CPUs online\n", g_count);
}

}  // namespace kernel::smp

extern "C" void ap_entry(kernel::smp::Cpu* c) {
  using namespace kernel;
  arch::x86_64::write_cr3(mm::kernel_root());
  arch::x86_64::load_gdt(c->descriptors, c->ist_tops);
  arch::x86_64::load_idt();
  smp::install_gs(c);
  platform::init_this_cpu();
  proc::init_syscall_this_cpu();
  kprint("smp: cpu {} online (apic {})\n", c->index, c->apic_id);
  __atomic_store_n(&c->online, true, __ATOMIC_RELEASE);
  sched::init_this_cpu(sched::kTickHz);
  sched::idle_loop();
}
