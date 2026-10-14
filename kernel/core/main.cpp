// M1: ядро-приёмник. Проверяет, что загрузчик выполнил контракт
// docs/abi/boot.md, печатает полученное и завершает QEMU. В M2 здесь
// начнётся настоящая инициализация.

#include <abi/boot.hpp>
#include <arch/x86_64/serial.hpp>
#include <lib/core/format.hpp>

#include "boot_info.hpp"

namespace {

using abi::boot::BootInfo;
using abi::boot::MemoryRegion;
using abi::boot::MemoryType;
using arch::x86_64::Serial;

void print_hex(const char* label, uint64_t v) {
  char buf[lib::core::kHex64Len];
  size_t n = lib::core::format_hex64(buf, sizeof buf, v);
  Serial::write(label);
  Serial::write(buf, n);
  Serial::write("\n");
}

void print_dec(const char* label, uint64_t v, const char* suffix = "\n") {
  char buf[20];
  size_t n = lib::core::format_dec(buf, sizeof buf, v);
  Serial::write(label);
  Serial::write(buf, n);
  Serial::write(suffix);
}

[[noreturn]] void panic(const char* what) {
  Serial::write("KERNEL PANIC: ");
  Serial::write(what);
  Serial::write("\n");
  arch::x86_64::qemu_exit(arch::x86_64::kExitFailure);
}

template <typename T>
T* phys_to_virt(uint64_t phys) {
  return reinterpret_cast<T*>(abi::boot::kHhdmBase + phys);
}

uint64_t read_cr0() { uint64_t v; asm volatile("mov %%cr0, %0" : "=r"(v)); return v; }

uint64_t rdmsr(uint32_t msr) {
  uint32_t lo, hi;
  asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
  return (uint64_t{hi} << 32) | lo;
}

uint64_t read_rflags() {
  uint64_t v;
  asm volatile("pushfq; pop %0" : "=r"(v));
  return v;
}

// Переменная в .bss: загрузчик обязан обнулить хвост сегмента.
constinit uint64_t g_bss_probe[64];

}  // namespace

extern "C" [[noreturn]] void kernel_main(uint64_t boot_info_phys, uint64_t stack_aligned) {
  Serial::write("CharmOS kernel: entry\n");

  const auto* info = phys_to_virt<const BootInfo>(boot_info_phys);
  if (info->magic != abi::boot::kMagic) {
    print_hex("  magic=", info->magic);
    print_hex("  want =", abi::boot::kMagic);
  }
  if (info->version_major != abi::boot::kVersionMajor) {
    print_dec("  version_major=", info->version_major);
    print_dec("  want         =", abi::boot::kVersionMajor);
  }
  if (const char* err = kernel::check_boot_info(*info)) panic(err);
  print_dec("boot: BootInfo v", info->version_major, ".");
  print_dec("", info->version_minor);

  if (stack_aligned == 0) panic("stack not aligned per boot ABI");
  if ((read_rflags() & (1u << 9)) != 0) panic("interrupts enabled at entry");
  if ((read_rflags() & (1u << 10)) != 0) panic("direction flag set at entry");
  if ((read_cr0() & (1u << 16)) == 0) panic("CR0.WP not set");
  if ((rdmsr(0xc0000080) & (1u << 11)) == 0) panic("EFER.NXE not set");
  for (uint64_t v : g_bss_probe) {
    if (v != 0) panic(".bss not zeroed");
  }
  auto self = reinterpret_cast<uint64_t>(&kernel_main);
  if (self < abi::boot::kKernelBase) panic("kernel not in higher half");
  print_hex("boot: kernel_main=", self);

  const auto* regions = phys_to_virt<const MemoryRegion>(info->memory_map);
  uint64_t usable = 0;
  for (uint64_t i = 0; i < info->memory_map_count; ++i) {
    const MemoryRegion& r = regions[i];
    if (i > 0 && regions[i - 1].base + regions[i - 1].length > r.base) {
      panic("memory map unsorted or overlapping");
    }
    if (r.base % 4096 != 0 || r.length % 4096 != 0) panic("memory region not page aligned");
    if (r.type == MemoryType::Usable) usable += r.length;
  }
  print_dec("boot: memory regions=", info->memory_map_count);
  print_dec("boot: usable MiB=", usable >> 20);
  print_hex("boot: kernel phys=", info->kernel_phys_base);

  const abi::boot::Framebuffer& fb = info->framebuffer;
  if (fb.base != 0) {
    print_dec("boot: framebuffer ", fb.width, "x");
    print_dec("", fb.height);
    // Запись через HHDM: левый верхний пиксель — белый.
    *phys_to_virt<volatile uint32_t>(fb.base) = 0x00ffffff;
  } else {
    Serial::write("boot: no framebuffer\n");
  }

  if (info->acpi_rsdp == 0) panic("no RSDP");
  const auto* rsdp = phys_to_virt<const char>(info->acpi_rsdp);
  const char kSig[] = "RSD PTR ";
  for (int i = 0; i < 8; ++i) {
    if (rsdp[i] != kSig[i]) panic("RSDP signature mismatch");
  }
  print_hex("boot: rsdp=", info->acpi_rsdp);

  if (info->cmdline != 0) {
    Serial::write("boot: cmdline=");
    Serial::write(phys_to_virt<const char>(info->cmdline), info->cmdline_length);
    Serial::write("\n");
  } else {
    Serial::write("boot: no cmdline\n");
  }

  Serial::write("CharmOS kernel: M1 OK\n");
  arch::x86_64::qemu_exit(arch::x86_64::kExitSuccess);
}
