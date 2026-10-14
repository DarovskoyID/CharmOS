#include "vmm.hpp"

#include <arch/x86_64/cpu.hpp>
#include <arch/x86_64/paging.hpp>

#include "../core/panic.hpp"
#include "../sync/spinlock.hpp"
#include "layout.hpp"
#include "pmm.hpp"

// Границы секций из kernel/linker.ld.
extern "C" const char __text_start[], __text_end[];
extern "C" const char __rodata_start[], __rodata_end[];
extern "C" const char __data_start[], __kernel_end[];

namespace kernel::mm {

using abi::boot::MemoryRegion;
using abi::boot::MemoryType;
using arch::x86_64::kMapCacheDisable;
using arch::x86_64::kMapNoExec;
using arch::x86_64::kMapWrite;
using arch::x86_64::kPageSize;
using arch::x86_64::PageTables;
using lib::core::Result;
using lib::core::Status;

namespace {

constinit PageTables g_tables;
constinit uint64_t g_mmio_next = kMmioBase;
// Защищает g_tables и g_mmio_next. Внутри берётся pmm (ранг выше).
constinit sync::Spinlock g_lock{sync::LockRank::Vmm, "vmm"};

uint64_t table_frame(void*) {
  auto r = alloc_frames();
  return r.is_ok() ? r.value() : 0;
}

uint64_t addr(const char* p) { return reinterpret_cast<uint64_t>(p); }

// Переносит страницы образа ядра из таблиц загрузчика в новые с правами секции.
void map_kernel_section(const PageTables& boot, const char* start, const char* end,
                        uint64_t flags, const char* name) {
  for (uint64_t v = addr(start); v < addr(end); v += kPageSize) {
    uint64_t phys = 0, old_flags = 0;
    if (!boot.translate(v, &phys, &old_flags)) panic("vmm: kernel page {X} not mapped", v);
    if (!g_tables.map_4k(v, phys, flags)) panic("vmm: cannot map kernel {} page {X}", name, v);
  }
}

bool hhdm_type(MemoryType t) {
  switch (t) {
    case MemoryType::Usable:
    case MemoryType::BootloaderReclaimable:
    case MemoryType::KernelImage:
    case MemoryType::AcpiReclaimable:
    case MemoryType::AcpiNvs:
    case MemoryType::Framebuffer:
    case MemoryType::Initrd:
      return true;
    default:
      return false;  // Reserved (в т.ч. MMIO прошивки) и BadMemory — не трогаем
  }
}

}  // namespace

void vmm_init(const abi::boot::BootInfo& info) {
  PageTables boot;
  boot.attach(arch::x86_64::read_cr3() & ~uint64_t{0xfff}, kHhdmBase);
  if (!g_tables.init(&table_frame, nullptr, kHhdmBase)) panic("vmm: no memory for PML4");

  map_kernel_section(boot, __text_start, __text_end, 0, "text");
  map_kernel_section(boot, __rodata_start, __rodata_end, kMapNoExec, "rodata");
  map_kernel_section(boot, __data_start, __kernel_end, kMapWrite | kMapNoExec, "data");

  const auto* regions = phys_to_virt<const MemoryRegion>(info.memory_map);
  uint64_t mapped = 0;
  for (uint64_t i = 0; i < info.memory_map_count; ++i) {
    const MemoryRegion& r = regions[i];
    if (!hhdm_type(r.type)) continue;
    if (!g_tables.map_range(kHhdmBase + r.base, r.base, r.length, kMapWrite | kMapNoExec)) {
      panic("vmm: cannot map HHDM region {x}+{x}", r.base, r.length);
    }
    mapped += r.length;
  }

  // Вся верхняя половина PML4 заполняется сразу: дальше отображения ядра
  // добавляются только на нижних уровнях, и копии этих записей в адресных
  // пространствах процессов не устаревают.
  for (size_t i = 256; i < 512; ++i) {
    if (!g_tables.ensure_pml4_entry(i)) panic("vmm: no memory for kernel PML4 entries");
  }

  arch::x86_64::write_cr3(g_tables.root());
  kprint("vmm: kernel tables active, HHDM {} MiB, identity map dropped\n", mapped >> 20);
}

uint64_t kernel_root() { return g_tables.root(); }

Status<VmError> map_new_page(uint64_t virt) {
  sync::SpinGuard guard(g_lock);
  auto frame = alloc_frames();
  if (frame.is_err()) return Status<VmError>::err(VmError::OutOfMemory);
  auto* p = phys_to_virt<uint64_t>(frame.value());
  for (size_t i = 0; i < kPageSize / 8; ++i) p[i] = 0;
  if (!g_tables.map_4k(virt, frame.value(), kMapWrite | kMapNoExec)) {
    free_frames(frame.value());
    return Status<VmError>::err(VmError::AlreadyMapped);
  }
  return lib::core::ok_status<VmError>();
}

Result<uint64_t, VmError> map_mmio(uint64_t phys, uint64_t size) {
  sync::SpinGuard guard(g_lock);
  uint64_t first = phys & ~(kPageSize - 1);
  uint64_t last = (phys + size + kPageSize - 1) & ~(kPageSize - 1);
  uint64_t span = last - first;
  if (span > kMmioBase + kMmioMax - g_mmio_next) {
    return Result<uint64_t, VmError>::err(VmError::OutOfAddressSpace);
  }
  uint64_t virt = g_mmio_next;
  for (uint64_t off = 0; off < span; off += kPageSize) {
    if (!g_tables.map_4k(virt + off, first + off, kMapWrite | kMapNoExec | kMapCacheDisable)) {
      return Result<uint64_t, VmError>::err(VmError::OutOfMemory);
    }
  }
  g_mmio_next += span;
  return Result<uint64_t, VmError>::ok(virt + (phys - first));
}

bool translate(uint64_t virt, uint64_t* phys, uint64_t* flags) {
  sync::SpinGuard guard(g_lock);
  return g_tables.translate(virt, phys, flags);
}

}  // namespace kernel::mm
