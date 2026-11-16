#include "../../kernel/fs/cache_index.hpp"
#include <assert.h>
#include <stdint.h>

int main() {
  kernel::fs::CacheIndex<8> index;
  assert(index.find(0, 0) == index.none);
  // Colliding keys, including high page bits and separate disks.
  for (uint32_t i = 0; i < 8; ++i) {
    assert(index.victim() == i);
    index.put(i, 0, uint64_t{i} * 16);
  }
  for (uint32_t i = 0; i < 8; ++i) assert(index.find(0, uint64_t{i} * 16) == i);
  assert(index.find(1, 0) == index.none);
  index.touch(0);
  assert(index.victim() == 1);
  index.put(1, 1, 0);
  assert(index.find(0, 16) == index.none);
  assert(index.find(0, 0) == 0 && index.find(1, 0) == 1);
  index.erase(4);
  assert(index.find(0, 64) == index.none && index.victim() == 4);
  index.erase(4);  // Failed fill can be retried without corrupting the lists.
  assert(index.victim() == 4);
  index.put(4, 3, uint64_t{1} << 48);
  assert(index.find(3, uint64_t{1} << 48) == 4);

  // Compare eviction and lookup against an independent age-based model.
  struct Model { uint32_t disk; uint64_t page, age; bool valid; } model[8] = {};
  kernel::fs::CacheIndex<8> tested;
  uint64_t clock = 0;
  uint32_t random = 12345;
  for (uint32_t step = 0; step < 50000; ++step) {
    random = random * 1664525u + 1013904223u;
    uint32_t disk = (random >> 24) % 3;
    uint64_t page = (random >> 8) % 40;
    uint32_t expected = 8;
    for (uint32_t i = 0; i < 8; ++i)
      if (model[i].valid && model[i].disk == disk && model[i].page == page) expected = i;
    auto actual = tested.find(disk, page);
    assert(actual == expected);
    if (actual != 8) {
      tested.touch(actual);
      model[actual].age = ++clock;
    } else {
      uint32_t victim = 0;
      for (uint32_t i = 1; i < 8; ++i)
        if (model[i].age < model[victim].age) victim = i;
      assert(tested.victim() == victim);
      tested.put(victim, disk, page);
      model[victim] = {disk, page, ++clock, true};
    }
  }
}
