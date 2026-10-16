#include <arch/x86_64/paging.hpp>
#include <arch/x86_64/cpu.hpp>

namespace arch::x86_64 {

namespace {

constexpr uint64_t kPresent = 1ull << 0;
constexpr uint64_t kWritable = 1ull << 1;
constexpr uint64_t kPageSizeBit = 1ull << 7;           // PS в PDPTE/PDE
constexpr uint64_t kAddrMask = 0x000ffffffffff000ull;  // биты 51:12
constexpr uint64_t kLeafFlagMask = kMapWrite | kMapUser | kMapCacheDisable | kMapWriteCombine | kMapNoExec;

size_t index(uint64_t virt, int level) {  // level: 3 = PML4 … 0 = PT
  return static_cast<size_t>((virt >> (12 + 9 * level)) & 0x1ff);
}

}  // namespace

uint64_t* PageTables::table(uint64_t phys) const {
  return reinterpret_cast<uint64_t*>(phys + offset_);
}

bool PageTables::init(PageAllocator alloc, void* ctx, uint64_t phys_offset) {
  alloc_ = alloc;
  ctx_ = ctx;
  offset_ = phys_offset;
  pml4_ = alloc_(ctx_);
  if (pml4_ == 0) return false;
  uint64_t* t = table(pml4_);
  for (size_t i = 0; i < 512; ++i) t[i] = 0;
  return true;
}

void PageTables::attach(uint64_t root, uint64_t phys_offset) {
  alloc_ = nullptr;
  pml4_ = root;
  offset_ = phys_offset;
}

uint64_t* PageTables::next_table(uint64_t* t, size_t i, bool user) {
  uint64_t e = t[i];
  if ((e & kPresent) != 0) {
    if ((e & kPageSizeBit) != 0) return nullptr;  // уже большой лист
    if (user) t[i] = e | kMapUser;
    return table(e & kAddrMask);
  }
  if (alloc_ == nullptr) return nullptr;
  uint64_t page = alloc_(ctx_);
  if (page == 0) return nullptr;
  uint64_t* fresh = table(page);
  for (size_t k = 0; k < 512; ++k) fresh[k] = 0;
  t[i] = page | kPresent | kWritable | (user ? kMapUser : 0);
  return fresh;
}

bool PageTables::map_4k(uint64_t virt, uint64_t phys, uint64_t flags) {
  if (virt % kPageSize != 0 || phys % kPageSize != 0) return false;
  uint64_t* t = table(pml4_);
  for (int level = 3; level > 0; --level) {
    t = next_table(t, index(virt, level), (flags & kMapUser) != 0);
    if (t == nullptr) return false;
  }
  uint64_t& e = t[index(virt, 0)];
  if ((e & kPresent) != 0) return false;
  e = phys | kPresent | (flags & kLeafFlagMask);
  return true;
}

bool PageTables::map_2m(uint64_t virt, uint64_t phys, uint64_t flags) {
  if (virt % kLargePageSize != 0 || phys % kLargePageSize != 0) return false;
  uint64_t* t = table(pml4_);
  for (int level = 3; level > 1; --level) {
    t = next_table(t, index(virt, level), (flags & kMapUser) != 0);
    if (t == nullptr) return false;
  }
  uint64_t& e = t[index(virt, 1)];
  if ((e & kPresent) != 0) return false;
  e = phys | kPresent | kPageSizeBit | (flags & kLeafFlagMask);
  return true;
}

bool PageTables::map_range(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags) {
  if (size % kPageSize != 0) return false;
  uint64_t off = 0;
  while (off < size) {
    uint64_t v = virt + off;
    uint64_t p = phys + off;
    if (v % kLargePageSize == 0 && p % kLargePageSize == 0 && size - off >= kLargePageSize) {
      if (!map_2m(v, p, flags)) return false;
      off += kLargePageSize;
    } else {
      if (!map_4k(v, p, flags)) return false;
      off += kPageSize;
    }
  }
  return true;
}

bool PageTables::translate(uint64_t virt, uint64_t* phys, uint64_t* flags) const {
  const uint64_t* t = table(pml4_);
  for (int level = 3; level >= 0; --level) {
    uint64_t e = t[index(virt, level)];
    if ((e & kPresent) == 0) return false;
    if (level == 2 && (e & kPageSizeBit) != 0) return false;  // 1 ГиБ не используем
    bool leaf = level == 0 || (level == 1 && (e & kPageSizeBit) != 0);
    if (leaf) {
      uint64_t page = level == 0 ? kPageSize : kLargePageSize;
      uint64_t base = e & kAddrMask & ~(page - 1);
      *phys = base + (virt & (page - 1));
      *flags = e & kLeafFlagMask;
      return true;
    }
    t = table(e & kAddrMask);
  }
  return false;
}

uint64_t PageTables::unmap_4k(uint64_t virt) {
  uint64_t* t = table(pml4_);
  for (int level = 3; level > 0; --level) {
    uint64_t e = t[index(virt, level)];
    if ((e & kPresent) == 0 || (e & kPageSizeBit) != 0) return 0;
    t = table(e & kAddrMask);
  }
  uint64_t& e = t[index(virt, 0)];
  if ((e & kPresent) == 0) return 0;
  uint64_t phys = e & kAddrMask;
  e = 0;
  return phys;
}

bool PageTables::ensure_pml4_entry(size_t i) {
  return next_table(table(pml4_), i, false) != nullptr;
}

void PageTables::destroy_lower_half(PageFree free_page, void* ctx) {
  uint64_t* pml4 = table(pml4_);
  for (size_t i4 = 0; i4 < 256; ++i4) {
    if ((pml4[i4] & kPresent) == 0) continue;
    uint64_t pdpt_phys = pml4[i4] & kAddrMask;
    uint64_t* pdpt = table(pdpt_phys);
    for (size_t i3 = 0; i3 < 512; ++i3) {
      if ((pdpt[i3] & kPresent) == 0) continue;
      uint64_t pd_phys = pdpt[i3] & kAddrMask;
      uint64_t* pd = table(pd_phys);
      for (size_t i2 = 0; i2 < 512; ++i2) {
        if ((pd[i2] & kPresent) == 0) continue;
        uint64_t pt_phys = pd[i2] & kAddrMask;
        uint64_t* pt = table(pt_phys);
        for (size_t i1 = 0; i1 < 512; ++i1) {
          if ((pt[i1] & kPresent) != 0) free_page(ctx, pt[i1] & kAddrMask);
        }
        free_page(ctx, pt_phys);
      }
      free_page(ctx, pd_phys);
    }
    free_page(ctx, pdpt_phys);
    pml4[i4] = 0;
  }
}

void init_pat() {
  // Табл. 12-10: 06 WB, 01 WC, 07 UC-, 00 UC, 04 WT. PA0..PA7 — байты 0..7.
  constexpr uint64_t kPat = 0x06ull | 0x01ull << 8 | 0x07ull << 16 | 0x00ull << 24 | 0x06ull << 32 |
                            0x04ull << 40 | 0x07ull << 48 | 0x00ull << 56;
  wrmsr(0x277, kPat);  // IA32_PAT
}

}  // namespace arch::x86_64
