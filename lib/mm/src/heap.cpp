#include <lib/mm/heap.hpp>

namespace lib::mm {

using lib::core::Result;
using lib::core::Status;

namespace {

constexpr uint64_t kUsedBit = 1;

}  // namespace

void Heap::set_tags(uintptr_t block, size_t size, bool is_used) {
  uint64_t tag = size | (is_used ? kUsedBit : 0);
  header(block) = tag;
  footer(block, size) = tag;
}

void Heap::list_push(uintptr_t block) {
  FreeNode* n = node(block);
  n->prev = nullptr;
  n->next = free_head_;
  if (free_head_ != nullptr) free_head_->prev = n;
  free_head_ = n;
}

void Heap::list_remove(uintptr_t block) {
  FreeNode* n = node(block);
  if (n->prev != nullptr) {
    n->prev->next = n->next;
  } else {
    free_head_ = n->next;
  }
  if (n->next != nullptr) n->next->prev = n->prev;
}

uintptr_t Heap::coalesce(uintptr_t block) {
  size_t size = size_of(header(block));
  uintptr_t next = block + size;
  if (!used(header(next))) {  // эпилог занят, за край не выйдем
    list_remove(next);
    size += size_of(header(next));
  }
  uint64_t prev_tag = *reinterpret_cast<uint64_t*>(block - 8);
  if (!used(prev_tag)) {  // пролог занят, за начало не выйдем
    uintptr_t prev = block - size_of(prev_tag);
    list_remove(prev);
    size += size_of(prev_tag);
    block = prev;
  }
  set_tags(block, size, false);
  return block;
}

bool Heap::grow(size_t at_least) {
  size_t step = (at_least + kGrowStep - 1) / kGrowStep * kGrowStep;
  if (step > max_ - mapped_) return false;
  uintptr_t at = base_ + mapped_;
  if (!grow_fn_(ctx_, at, step)) return false;
  // Старый эпилог (последние 8 Б) становится заголовком нового блока.
  uintptr_t block = at - 8;
  mapped_ += step;
  set_tags(block, step, false);
  header(block + step) = kUsedBit;  // новый эпилог
  list_push(coalesce(block));
  return true;
}

Status<HeapError> Heap::init(uintptr_t base, size_t max_size, GrowFn grow_fn, void* ctx) {
  using S = Status<HeapError>;
  if (base % 4096 != 0 || max_size < kGrowStep || max_size % kGrowStep != 0 ||
      grow_fn == nullptr) {
    return S::err(HeapError::InvalidArgument);
  }
  base_ = base;
  max_ = max_size;
  grow_fn_ = grow_fn;
  ctx_ = ctx;
  used_ = 0;
  free_head_ = nullptr;
  if (!grow_fn_(ctx_, base_, kGrowStep)) return S::err(HeapError::OutOfMemory);
  mapped_ = kGrowStep;
  *reinterpret_cast<uint64_t*>(base_) = kUsedBit;  // пролог
  uintptr_t first = base_ + 8;
  set_tags(first, kGrowStep - 16, false);
  header(base_ + kGrowStep - 8) = kUsedBit;  // эпилог
  list_push(first);
  return lib::core::ok_status<HeapError>();
}

Result<void*, HeapError> Heap::alloc(size_t size) {
  using R = Result<void*, HeapError>;
  if (size > max_) return R::err(HeapError::OutOfMemory);
  if (size == 0) size = 1;
  size_t need = (size + 16 + kAlign - 1) / kAlign * kAlign;
  if (need < kMinBlock) need = kMinBlock;

  for (int attempt = 0; attempt < 2; ++attempt) {
    for (FreeNode* n = free_head_; n != nullptr; n = n->next) {
      uintptr_t block = block_of(n);
      size_t have = size_of(header(block));
      if (have < need) continue;
      list_remove(block);
      if (have - need >= kMinBlock) {
        set_tags(block, need, true);
        uintptr_t rest = block + need;
        set_tags(rest, have - need, false);
        list_push(rest);
      } else {
        set_tags(block, have, true);
        need = have;
      }
      used_ += need;
      return R::ok(reinterpret_cast<void*>(block + 8));
    }
    // Не нашлось: добираем память (новый блок сольётся с последним свободным).
    if (attempt == 0 && !grow(need)) break;
  }
  return R::err(HeapError::OutOfMemory);
}

Status<HeapError> Heap::free(void* ptr) {
  using S = Status<HeapError>;
  auto p = reinterpret_cast<uintptr_t>(ptr);
  if (p % kAlign != 0 || p < base_ + 16 || p >= base_ + mapped_ - 8) {
    return S::err(HeapError::InvalidPointer);
  }
  uintptr_t block = p - 8;
  uint64_t tag = header(block);
  size_t size = size_of(tag);
  if (!used(tag) || size < kMinBlock || block + size > base_ + mapped_ - 8) {
    return S::err(HeapError::InvalidPointer);
  }
  if (footer(block, size) != tag) return S::err(HeapError::Corrupted);
  used_ -= size;
  set_tags(block, size, false);
  list_push(coalesce(block));
  return lib::core::ok_status<HeapError>();
}

size_t Heap::free_list_length() const {
  size_t n = 0;
  for (FreeNode* f = free_head_; f != nullptr; f = f->next) ++n;
  return n;
}

bool Heap::check() const {
  if (mapped_ == 0) return false;
  if (*reinterpret_cast<const uint64_t*>(base_) != kUsedBit) return false;
  uintptr_t end = base_ + mapped_ - 8;
  if (header(end) != kUsedBit) return false;

  size_t free_blocks = 0;
  size_t used_total = 0;
  bool prev_free = false;
  uintptr_t b = base_ + 8;
  while (b < end) {
    uint64_t tag = header(b);
    size_t size = size_of(tag);
    if (size < kMinBlock || b + size > end) return false;
    if (footer(b, size) != tag) return false;
    if (!used(tag)) {
      if (prev_free) return false;  // соседние свободные должны были слиться
      ++free_blocks;
    } else {
      used_total += size;
    }
    prev_free = !used(tag);
    b += size;
  }
  if (b != end) return false;
  if (used_total != used_) return false;

  size_t listed = 0;
  const FreeNode* prev = nullptr;
  for (const FreeNode* f = free_head_; f != nullptr; f = f->next) {
    if (f->prev != prev) return false;
    if (used(header(block_of(f)))) return false;
    prev = f;
    if (++listed > free_blocks) return false;
  }
  return listed == free_blocks;
}

}  // namespace lib::mm
