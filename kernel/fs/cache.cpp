#include "cache.hpp"
#include "cache_index.hpp"

#include <arch/x86_64/paging.hpp>

#include "../block/block.hpp"
#include "../mm/layout.hpp"
#include "../mm/pmm.hpp"
#include "../platform/platform.hpp"
#include "../sync/wait.hpp"

namespace kernel::fs {

namespace {

using arch::x86_64::kPageSize;
using lib::fs::FsError;

constexpr uint32_t kCachePages = 4096;  // 16 MiB; holds read-ahead plus hot filesystem metadata.
static_assert(kCachePages > block::kMaxPages);

struct Page {
  uint64_t phys;  // 0 — слот ещё без фрейма
  uint32_t disk;
  uint64_t index;  // номер страницы диска (байт index * 4096)
  bool valid;
  bool dirty;
};

constinit sync::Mutex g_lock{"fs.cache"};
constinit Page g_pages[kCachePages] = {};
constinit CacheIndex<kCachePages> g_index;
constinit CacheStats g_stats = {};
// DiskIo serializes every access with g_lock. Reuse batch scratch here rather
// than consuming most of a kernel stack during a nested filesystem read.
constinit Page* g_batch_pages[block::kMaxPages] = {};
constinit uint64_t g_batch_frames[block::kMaxPages] = {};

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
  uint32_t slot = g_index.find(disk, index);
  if (slot != CacheIndex<kCachePages>::none) {
    g_index.touch(slot);
    ++g_stats.hits;
    return &g_pages[slot];
  }
  slot = g_index.victim();
  Page* victim = &g_pages[slot];
  ++g_stats.misses;
  if (victim->valid && victim->dirty && write_back(*victim).is_err()) return nullptr;
  g_index.erase(slot);
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
  *victim = {victim->phys, disk, index, true, false};
  g_index.put(slot, disk, index);
  return victim;
}

Page* cached(uint32_t disk, uint64_t index) {
  uint32_t slot = g_index.find(disk, index);
  return slot == CacheIndex<kCachePages>::none ? nullptr : &g_pages[slot];
}

// Reserve consecutive missing pages under g_lock, then fill them in one existing
// submission batch of existing block-ABI requests. A cached page (including a dirty one) ends the batch.
Page* read_pages(uint32_t disk, uint64_t index, uint32_t wanted) {
  if (cached(disk, index)) return get(disk, index, true);
  auto* pages = g_batch_pages;
  auto* frames = g_batch_frames;
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
    uint64_t started = platform::now_ns();
    ok = block::transfer(disk, false, lba, frames, count) == block::Status::Ok;
    g_stats.read_io_ns += platform::now_ns() - started;
  }
  if (!ok) {
    for (uint32_t i = 0; i < count; ++i) {
      g_index.erase(static_cast<uint32_t>(pages[i] - g_pages));
      pages[i]->valid = false;
    }
    return nullptr;
  }
  return pages[0];
}

lib::fs::Status write_pages(Page& first) {
  auto* pages = g_batch_pages;
  auto* frames = g_batch_frames;
  pages[0] = &first;
  frames[0] = first.phys;
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
    // Read-ahead: small reads are usually sequential (font tables, ELF
    // headers, directory blocks). Prefetch a window so the following calls hit
    // the page cache instead of issuing one 4 KiB device request each.
    // Full ELF extents benefit from large batches; tiny table/header reads
    // should not pull six MiB of unrelated data into the cache.
    const uint32_t read_ahead = len - done >= 64 * 1024 ? block::kMaxPages : 16;
    if (wanted < read_ahead) {
      const uint64_t base = abs / kPageSize;
      const uint64_t file_pages = (offset_ + size_ + kPageSize - 1) / kPageSize;
      const uint64_t remain = file_pages > base ? file_pages - base : 0;
      const uint32_t ahead = remain > read_ahead
                                 ? read_ahead
                                 : static_cast<uint32_t>(remain);
      if (ahead > wanted) wanted = ahead;
    }
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
