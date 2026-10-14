#include <arch/x86_64/paging.hpp>

namespace arch::x86_64 {

namespace {

constexpr uint64_t kPresent = 1ull << 0;
constexpr uint64_t kWritable = 1ull << 1;
constexpr uint64_t kPageSizeBit = 1ull << 7;           // PS в PDPTE/PDE
constexpr uint64_t kAddrMask = 0x000ffffffffff000ull;  // биты 51:12
constexpr uint64_t kLeafFlagMask = kMapWrite | kMapCacheDisable | kMapNoExec;

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

uint64_t* PageTables::next_table(uint64_t* t, size_t i) {
  uint64_t e = t[i];
  if ((e & kPresent) != 0) {
    if ((e & kPageSizeBit) != 0) return nullptr;  // уже большой лист
    return table(e & kAddrMask);
  }
  if (alloc_ == nullptr) return nullptr;
  uint64_t page = alloc_(ctx_);
  if (page == 0) return nullptr;
  uint64_t* fresh = table(page);
  for (size_t k = 0; k < 512; ++k) fresh[k] = 0;
  t[i] = page | kPresent | kWritable;
  return fresh;
}

bool PageTables::map_4k(uint64_t virt, uint64_t phys, uint64_t flags) {
  if (virt % kPageSize != 0 || phys % kPageSize != 0) return false;
  uint64_t* t = table(pml4_);
  for (int level = 3; level > 0; --level) {
    t = next_table(t, index(virt, level));
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
    t = next_table(t, index(virt, level));
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

}  // namespace arch::x86_64
