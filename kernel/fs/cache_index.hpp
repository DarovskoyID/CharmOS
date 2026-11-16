#pragma once

#include <stdint.h>

namespace kernel::fs {

// Fixed-capacity lookup and replacement order. The caller owns page contents
// and serializes operations; replacing a dirty slot requires writeback first.
template <uint32_t N>
class CacheIndex {
 public:
  static_assert(N > 0 && (N & (N - 1)) == 0);
  static constexpr uint32_t none = N;

  uint32_t find(uint32_t disk, uint64_t page) {
    init();
    for (uint32_t i = buckets_[hash(disk, page)]; i != none; i = slots_[i].chain)
      if (slots_[i].disk == disk && slots_[i].page == page) return i;
    return none;
  }
  uint32_t victim() { init(); return oldest_; }

  void touch(uint32_t i) {
    init();
    if (i == newest_) return;
    detach(i);
    slots_[i].prev = newest_;
    slots_[i].next = none;
    slots_[newest_].next = i;
    newest_ = i;
  }
  void erase(uint32_t i) {
    init();
    auto& s = slots_[i];
    if (s.valid) {
      auto* link = &buckets_[hash(s.disk, s.page)];
      // Defensive bound: a valid slot must be in its bucket; never walk past
      // the sentinel if an invariant is broken elsewhere.
      while (*link != i && *link != none) link = &slots_[*link].chain;
      if (*link == i) *link = s.chain;
      s.valid = false;
    }
    // Failed fills must be reusable before any valid cached page is evicted.
    if (i == oldest_) return;
    detach(i);
    s.prev = none;
    s.next = oldest_;
    slots_[oldest_].prev = i;
    oldest_ = i;
  }
  void put(uint32_t i, uint32_t disk, uint64_t page) {
    erase(i);
    auto& s = slots_[i];
    auto& bucket = buckets_[hash(disk, page)];
    s.disk = disk;
    s.page = page;
    s.chain = bucket;
    s.valid = true;
    bucket = i;
    touch(i);
  }

 private:
  struct Slot {
    uint64_t page;
    uint32_t disk, chain, prev, next;
    bool valid;
  };
  Slot slots_[N] = {};
  uint32_t buckets_[2 * N] = {};
  uint32_t oldest_ = 0, newest_ = N - 1;
  bool ready_ = false;

  static uint32_t hash(uint32_t disk, uint64_t page) {
    return static_cast<uint32_t>(page ^ (page >> 32) ^
                                 (uint64_t{disk} * 0x9e3779b9u)) & (2 * N - 1);
  }
  void init() {
    if (ready_) return;
    for (auto& bucket : buckets_) bucket = none;
    for (uint32_t i = 0; i < N; ++i) {
      slots_[i].prev = i ? i - 1 : none;
      slots_[i].next = i + 1 < N ? i + 1 : none;
    }
    ready_ = true;
  }
  void detach(uint32_t i) {
    auto& s = slots_[i];
    if (s.prev != none) slots_[s.prev].next = s.next;
    else oldest_ = s.next;
    if (s.next != none) slots_[s.next].prev = s.prev;
    else newest_ = s.prev;
  }
};

}  // namespace kernel::fs
