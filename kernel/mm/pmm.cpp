#include "pmm.hpp"

#include "../core/panic.hpp"
#include "../sync/spinlock.hpp"
#include "layout.hpp"

namespace kernel::mm {

using abi::boot::MemoryRegion;
using abi::boot::MemoryType;
using lib::mm::FrameBitmap;
using lib::mm::kFrameSize;

namespace {

constinit FrameBitmap g_frames;
constinit uint64_t g_total = 0;
constinit sync::Spinlock g_lock{sync::LockRank::Pmm, "pmm"};

bool is_ram(MemoryType t) {
  return t == MemoryType::Usable || t == MemoryType::BootloaderReclaimable ||
         t == MemoryType::KernelImage || t == MemoryType::AcpiReclaimable ||
         t == MemoryType::Initrd;
}

}  // namespace

void pmm_init(const abi::boot::BootInfo& info) {
  const auto* regions = phys_to_virt<const MemoryRegion>(info.memory_map);
  uint64_t count = info.memory_map_count;

  uint64_t top = 0;
  for (uint64_t i = 0; i < count; ++i) {
    if (is_ram(regions[i].type) && regions[i].base + regions[i].length > top) {
      top = regions[i].base + regions[i].length;
    }
  }
  uint64_t frames = top / kFrameSize;
  uint64_t bitmap_bytes = FrameBitmap::words_for(frames) * 8;
  uint64_t bitmap_size = (bitmap_bytes + kFrameSize - 1) / kFrameSize * kFrameSize;

  // Карту кладём в первый подходящий свободный регион (не в нулевой фрейм).
  uint64_t bitmap_phys = 0;
  for (uint64_t i = 0; i < count && bitmap_phys == 0; ++i) {
    const MemoryRegion& r = regions[i];
    uint64_t base = r.base == 0 ? kFrameSize : r.base;
    if (r.type == MemoryType::Usable && r.base + r.length >= base + bitmap_size) {
      bitmap_phys = base;
    }
  }
  if (bitmap_phys == 0) panic("pmm: no room for frame bitmap ({} bytes)", bitmap_size);

  g_frames.init(phys_to_virt<uint64_t>(bitmap_phys), frames);
  for (uint64_t i = 0; i < count; ++i) {
    if (regions[i].type == MemoryType::Usable) {
      g_frames.mark_free(regions[i].base, regions[i].length);
    }
  }
  g_frames.mark_used(bitmap_phys, bitmap_size);
  g_frames.mark_used(0, kFrameSize);  // физ. адрес 0 — признак «нет фрейма»
  g_total = g_frames.free_frames();
  g_frames.set_preferred_start(1 << 20);  // ниже 1 МиБ — для трамплина AP

  kprint("pmm: {} MiB free, bitmap {} KiB at {x}\n", g_total * kFrameSize >> 20,
         bitmap_size >> 10, bitmap_phys);
}

lib::core::Result<uint64_t, FrameError> alloc_frames(uint64_t count, uint64_t align_frames) {
  sync::SpinGuard guard(g_lock);
  return g_frames.alloc(count, align_frames);
}

lib::core::Result<uint64_t, FrameError> alloc_frames_below(uint64_t limit_phys, uint64_t count) {
  sync::SpinGuard guard(g_lock);
  return g_frames.alloc_below(limit_phys, count);
}

void free_frames(uint64_t phys, uint64_t count) {
  lib::core::Status<FrameError> r = lib::core::ok_status<FrameError>();
  {
    sync::SpinGuard guard(g_lock);
    r = g_frames.free(phys, count);
  }
  if (r.is_err()) panic("pmm: bad free of {x} x{} (error {})", phys, count, r.error());
}

uint64_t free_frame_count() { return g_frames.free_frames(); }
uint64_t total_frame_count() { return g_total; }

}  // namespace kernel::mm
