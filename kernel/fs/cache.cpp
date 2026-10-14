#include "cache.hpp"

#include <arch/x86_64/paging.hpp>

#include "../block/block.hpp"
#include "../mm/layout.hpp"
#include "../mm/pmm.hpp"
#include "../sync/wait.hpp"

namespace kernel::fs {

namespace {

using arch::x86_64::kPageSize;
using lib::fs::FsError;

constexpr uint32_t kCachePages = 2048;  // 8 МиБ

struct Page {
  uint64_t phys;  // 0 — слот ещё без фрейма
  uint32_t disk;
  uint64_t index;  // номер страницы диска (байт index * 4096)
  bool valid;
  bool dirty;
  uint64_t last_use;
};

constinit sync::Mutex g_lock{"fs.cache"};
constinit Page g_pages[kCachePages] = {};
constinit uint64_t g_clock = 0;
constinit CacheStats g_stats = {};

// Блоков диска в странице и номер первого блока страницы.
bool page_lba(uint32_t disk, uint64_t index, uint64_t* lba) {
  block::DiskInfo info;
  if (!block::disk_info(disk, &info) || info.block_size > kPageSize) return false;
  *lba = index * (kPageSize / info.block_size);
  return true;
}

lib::fs::Status write_back(Page& p) {
  uint64_t lba;
  if (!page_lba(p.disk, p.index, &lba)) return lib::fs::fail(FsError::IoError);
  if (block::transfer(p.disk, true, lba, &p.phys, 1) != block::Status::Ok) {
    return lib::fs::fail(FsError::IoError);
  }
  p.dirty = false;
  ++g_stats.writebacks;
  return lib::fs::ok();
}

// Страница диска в кэше (под g_lock). fill — прочитать с диска при промахе.
Page* get(uint32_t disk, uint64_t index, bool fill) {
  Page* victim = nullptr;
  for (Page& p : g_pages) {
    if (p.valid && p.disk == disk && p.index == index) {
      p.last_use = ++g_clock;
      ++g_stats.hits;
      return &p;
    }
    if (!p.valid) {
      if (victim == nullptr || victim->valid) victim = &p;
    } else if (victim == nullptr || (victim->valid && p.last_use < victim->last_use)) {
      victim = &p;
    }
  }
  ++g_stats.misses;
  if (victim->valid && victim->dirty && write_back(*victim).is_err()) return nullptr;
  victim->valid = false;
  if (victim->phys == 0) {
    auto f = mm::alloc_frames();
    if (f.is_err()) return nullptr;
    victim->phys = f.value();
  }
  if (fill) {
    uint64_t lba;
    if (!page_lba(disk, index, &lba) ||
        block::transfer(disk, false, lba, &victim->phys, 1) != block::Status::Ok) {
      return nullptr;
    }
  } else {
    __builtin_memset(mm::phys_to_virt<void>(victim->phys), 0, kPageSize);
  }
  *victim = {victim->phys, disk, index, true, false, ++g_clock};
  return victim;
}

}  // namespace

lib::fs::Status DiskIo::read(uint64_t offset, void* buf, size_t len) {
  if (offset > size_ || len > size_ - offset) return lib::fs::fail(FsError::IoError);
  sync::MutexGuard guard(g_lock);
  auto* out = static_cast<uint8_t*>(buf);
  for (size_t done = 0; done < len;) {
    uint64_t abs = offset_ + offset + done;
    uint64_t in = abs % kPageSize;
    size_t chunk = kPageSize - in < len - done ? static_cast<size_t>(kPageSize - in) : len - done;
    Page* p = get(disk_, abs / kPageSize, true);
    if (p == nullptr) return lib::fs::fail(FsError::IoError);
    __builtin_memcpy(out + done, mm::phys_to_virt<uint8_t>(p->phys) + in, chunk);
    done += chunk;
  }
  return lib::fs::ok();
}

lib::fs::Status DiskIo::write(uint64_t offset, const void* buf, size_t len) {
  if (offset > size_ || len > size_ - offset) return lib::fs::fail(FsError::IoError);
  sync::MutexGuard guard(g_lock);
  const auto* in_buf = static_cast<const uint8_t*>(buf);
  for (size_t done = 0; done < len;) {
    uint64_t abs = offset_ + offset + done;
    uint64_t in = abs % kPageSize;
    size_t chunk = kPageSize - in < len - done ? static_cast<size_t>(kPageSize - in) : len - done;
    // Страница перезаписывается целиком — читать её с диска незачем.
    Page* p = get(disk_, abs / kPageSize, chunk != kPageSize);
    if (p == nullptr) return lib::fs::fail(FsError::IoError);
    __builtin_memcpy(mm::phys_to_virt<uint8_t>(p->phys) + in, in_buf + done, chunk);
    p->dirty = true;
    done += chunk;
  }
  return lib::fs::ok();
}

lib::fs::Status DiskIo::flush() {
  sync::MutexGuard guard(g_lock);
  for (Page& p : g_pages) {
    if (p.valid && p.dirty && p.disk == disk_ && write_back(p).is_err()) {
      return lib::fs::fail(FsError::IoError);
    }
  }
  return block::flush(disk_) == block::Status::Ok ? lib::fs::ok() : lib::fs::fail(FsError::IoError);
}

CacheStats cache_stats() {
  sync::MutexGuard guard(g_lock);
  return g_stats;
}

}  // namespace kernel::fs
