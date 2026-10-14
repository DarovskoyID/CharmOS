#include <cstdlib>

#include "paging.hpp"
#include "test.hpp"

using boot::PageTables;

namespace {

// На хосте физический адрес таблицы — её адрес в памяти процесса.
struct Pool {
  size_t used = 0;
  size_t cap;
  explicit Pool(size_t c) : cap(c) {}
  static uint64_t alloc(void* ctx) {
    auto* self = static_cast<Pool*>(ctx);
    if (self->used == self->cap) return 0;
    ++self->used;
    void* p = std::aligned_alloc(4096, 4096);
    std::memset(p, 0, 4096);
    return reinterpret_cast<uint64_t>(p);  // утечка в тесте допустима
  }
};

constexpr uint64_t kHi = 0xffffffff80000000ull;

}  // namespace

TEST(paging_map_4k_and_translate) {
  Pool pool(64);
  PageTables pt;
  CHECK(pt.init(&Pool::alloc, &pool));
  CHECK(pt.map_4k(kHi, 0x123000, boot::kMapNoExec));
  uint64_t phys = 0, flags = 0;
  CHECK(pt.translate(kHi + 0x45, &phys, &flags));
  CHECK(phys == 0x123045);
  CHECK(flags == boot::kMapNoExec);
  CHECK(!pt.translate(kHi + 0x1000, &phys, &flags));
  CHECK(pool.used == 4);  // PML4 + PDPT + PD + PT
}

TEST(paging_map_2m) {
  Pool pool(64);
  PageTables pt;
  pt.init(&Pool::alloc, &pool);
  CHECK(pt.map_2m(0xffff800000200000ull, 0x200000, boot::kMapWrite | boot::kMapNoExec));
  uint64_t phys = 0, flags = 0;
  CHECK(pt.translate(0xffff8000003fffffull, &phys, &flags));
  CHECK(phys == 0x3fffff);
  CHECK(flags == (boot::kMapWrite | boot::kMapNoExec));
}

TEST(paging_rejects_double_map_and_misalignment) {
  Pool pool(64);
  PageTables pt;
  pt.init(&Pool::alloc, &pool);
  CHECK(pt.map_4k(kHi, 0x1000, 0));
  CHECK(!pt.map_4k(kHi, 0x2000, 0));
  CHECK(!pt.map_4k(kHi + 1, 0x2000, 0));
  CHECK(!pt.map_2m(0x200000, 0x1000, 0));
  CHECK(pt.map_2m(0x200000, 0x200000, 0));
  CHECK(!pt.map_4k(0x201000, 0x1000, 0));  // внутри большой страницы
}

TEST(paging_map_range_mixes_page_sizes) {
  Pool pool(64);
  PageTables pt;
  pt.init(&Pool::alloc, &pool);
  // 1 МиБ мелких страниц до границы 2 МиБ, две большие, затем хвост 12 КиБ.
  CHECK(pt.map_range(0x100000, 0x100000, 0x100000 + 0x400000 + 0x3000, boot::kMapWrite));
  uint64_t phys = 0, flags = 0;
  CHECK(pt.translate(0x100000, &phys, &flags) && phys == 0x100000);
  CHECK(pt.translate(0x3ff000, &phys, &flags) && phys == 0x3ff000);
  CHECK(pt.translate(0x502000, &phys, &flags) && phys == 0x502000);
  CHECK(pt.translate(0x602000, &phys, &flags) && phys == 0x602000);
  CHECK(!pt.translate(0x603000, &phys, &flags));
  CHECK(pool.used == 5);  // PML4 + PDPT + PD + 2 PT (хвосты до и после больших страниц)
}

TEST(paging_out_of_memory) {
  Pool pool(2);
  PageTables pt;
  CHECK(pt.init(&Pool::alloc, &pool));
  CHECK(!pt.map_4k(kHi, 0x1000, 0));
}
