#include "paging.hpp"

namespace boot {

namespace {

constexpr uint64_t kPresent = 1ull << 0;
constexpr uint64_t kWritable = 1ull << 1;
constexpr uint64_t kPageSizeBit = 1ull << 7;           // PS в PDPTE/PDE
constexpr uint64_t kAddrMask = 0x000ffffffffff000ull;  // биты 51:12
constexpr uint64_t kLeafFlagMask = kMapWrite | kMapNoExec;

uint64_t* table_at(uint64_t phys) { return reinterpret_cast<uint64_t*>(phys); }

size_t index(uint64_t virt, int level) {  // level: 3 = PML4 … 0 = PT
  return static_cast<size_t>((virt >> (12 + 9 * level)) & 0x1ff);
}

}  // namespace

bool PageTables::init(PageAllocator alloc, void* ctx) {
  alloc_ = alloc;
  ctx_ = ctx;
  pml4_ = alloc_(ctx_);
  return pml4_ != 0;
}

uint64_t* PageTables::next_table(uint64_t* table, size_t i) {
  uint64_t e = table[i];
  if ((e & kPresent) != 0) {
    if ((e & kPageSizeBit) != 0) return nullptr;  // уже большой лист
    return table_at(e & kAddrMask);
  }
  uint64_t page = alloc_(ctx_);
  if (page == 0) return nullptr;
  table[i] = page | kPresent | kWritable;
  return table_at(page);
}

bool PageTables::map_4k(uint64_t virt, uint64_t phys, uint64_t flags) {
  if (virt % kPageSize != 0 || phys % kPageSize != 0) return false;
  uint64_t* t = table_at(pml4_);
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
  uint64_t* t = table_at(pml4_);
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
  const uint64_t* t = table_at(pml4_);
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
    t = table_at(e & kAddrMask);
  }
  return false;
}

}  // namespace boot
