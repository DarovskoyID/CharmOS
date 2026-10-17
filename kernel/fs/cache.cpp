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
static_assert(kCachePages > block::kMaxPages);

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
  ++g_stats.write_requests;
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
    ++g_stats.read_requests;
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

Page* cached(uint32_t disk, uint64_t index) {
  for (Page& p : g_pages)
    if (p.valid && p.disk == disk && p.index == index) return &p;
  return nullptr;
}

// Reserve consecutive missing pages under g_lock, then fill them in one existing
// block-ABI request. A cached page (including a dirty one) ends the batch.
Page* read_pages(uint32_t disk, uint64_t index, uint32_t wanted) {
  if (cached(disk, index)) return get(disk, index, true);
  Page* pages[block::kMaxPages] = {};
  uint64_t frames[block::kMaxPages] = {};
  uint32_t count = 0;
  while (count < wanted && !cached(disk, index + count)) {
    Page* p = get(disk, index + count, false);
    if (!p) break;
    pages[count] = p;
    frames[count++] = p->phys;
  }
  uint64_t lba = 0;
  bool ok = count != 0 && page_lba(disk, index, &lba);
  if (ok) {
    ++g_stats.read_requests;
    ok = block::transfer(disk, false, lba, frames, count) == block::Status::Ok;
  }
  if (!ok) {
    for (uint32_t i = 0; i < count; ++i) pages[i]->valid = false;
    return nullptr;
  }
  return pages[0];
}

lib::fs::Status write_pages(Page& first) {
  Page* pages[block::kMaxPages] = {&first};
  uint64_t frames[block::kMaxPages] = {first.phys};
  uint32_t count = 1;
  while (count < block::kMaxPages) {
    Page* next = cached(first.disk, first.index + count);
    if (!next || !next->dirty) break;
    pages[count] = next;
    frames[count++] = next->phys;
  }
  uint64_t lba;
  if (!page_lba(first.disk, first.index, &lba)) return lib::fs::fail(FsError::IoError);
  ++g_stats.write_requests;
  if (block::transfer(first.disk, true, lba, frames, count) != block::Status::Ok)
    return lib::fs::fail(FsError::IoError);
  for (uint32_t i = 0; i < count; ++i) pages[i]->dirty = false;
  g_stats.writebacks += count;
  return lib::fs::ok();
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
    uint64_t available = in == 0 ? (len - done) / kPageSize : 0;
    uint32_t wanted = available > block::kMaxPages ? block::kMaxPages : static_cast<uint32_t>(available);
    if (!wanted) wanted = 1;
    Page* p = read_pages(disk_, abs / kPageSize, wanted);
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
    if (p.valid && p.dirty && p.disk == disk_ && write_pages(p).is_err()) {
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
