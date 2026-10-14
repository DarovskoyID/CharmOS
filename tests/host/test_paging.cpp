#include <cstdlib>

#include <arch/x86_64/paging.hpp>
#include "test.hpp"

using arch::x86_64::PageTables;

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
  CHECK(pt.init(&Pool::alloc, &pool, 0));
  CHECK(pt.map_4k(kHi, 0x123000, arch::x86_64::kMapNoExec));
  uint64_t phys = 0, flags = 0;
  CHECK(pt.translate(kHi + 0x45, &phys, &flags));
  CHECK(phys == 0x123045);
  CHECK(flags == arch::x86_64::kMapNoExec);
  CHECK(!pt.translate(kHi + 0x1000, &phys, &flags));
  CHECK(pool.used == 4);  // PML4 + PDPT + PD + PT
}

TEST(paging_map_2m) {
  Pool pool(64);
  PageTables pt;
  pt.init(&Pool::alloc, &pool, 0);
  CHECK(pt.map_2m(0xffff800000200000ull, 0x200000, arch::x86_64::kMapWrite | arch::x86_64::kMapNoExec));
  uint64_t phys = 0, flags = 0;
  CHECK(pt.translate(0xffff8000003fffffull, &phys, &flags));
  CHECK(phys == 0x3fffff);
  CHECK(flags == (arch::x86_64::kMapWrite | arch::x86_64::kMapNoExec));
}

TEST(paging_rejects_double_map_and_misalignment) {
  Pool pool(64);
  PageTables pt;
  pt.init(&Pool::alloc, &pool, 0);
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
  pt.init(&Pool::alloc, &pool, 0);
  // 1 МиБ мелких страниц до границы 2 МиБ, две большие, затем хвост 12 КиБ.
  CHECK(pt.map_range(0x100000, 0x100000, 0x100000 + 0x400000 + 0x3000, arch::x86_64::kMapWrite));
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
  CHECK(pt.init(&Pool::alloc, &pool, 0));
  CHECK(!pt.map_4k(kHi, 0x1000, 0));
}

TEST(paging_user_flag_reaches_every_level) {
  Pool pool(64);
  PageTables pt;
  pt.init(&Pool::alloc, &pool, 0);
  CHECK(pt.map_4k(0x400000, 0x9000, arch::x86_64::kMapWrite));  // таблицы созданы без U/S
  CHECK(pt.map_4k(0x401000, 0xa000, arch::x86_64::kMapUser));    // тот же путь, теперь user
  auto* pml4 = reinterpret_cast<uint64_t*>(pt.root());
  CHECK((pml4[0] & arch::x86_64::kMapUser) != 0);
  uint64_t phys = 0, flags = 0;
  CHECK(pt.translate(0x401000, &phys, &flags) && (flags & arch::x86_64::kMapUser) != 0);
  CHECK(pt.translate(0x400000, &phys, &flags) && (flags & arch::x86_64::kMapUser) == 0);
}

namespace {
struct Freed {
  size_t count = 0;
  static void free(void* ctx, uint64_t) { ++static_cast<Freed*>(ctx)->count; }
};
}  // namespace

TEST(paging_destroy_lower_half) {
  Pool pool(64);
  PageTables pt;
  pt.init(&Pool::alloc, &pool, 0);
  CHECK(pt.map_4k(0x400000, 0x9000, arch::x86_64::kMapUser));
  CHECK(pt.map_4k(0x401000, 0xa000, arch::x86_64::kMapUser));
  CHECK(pt.map_4k(0x7ffffffff000ull, 0xb000, arch::x86_64::kMapUser));
  CHECK(pt.map_4k(0xffffffff80000000ull, 0xc000, 0));  // верхняя половина — не трогать
  Freed f;
  pt.destroy_lower_half(&Freed::free, &f);
  // 3 листа + (PDPT+PD+PT) для 0x400000 + (PDPT+PD+PT) для 0x7ffffffff000.
  CHECK(f.count == 3 + 3 + 3);
  uint64_t phys = 0, flags = 0;
  CHECK(!pt.translate(0x400000, &phys, &flags));
  CHECK(pt.translate(0xffffffff80000000ull, &phys, &flags) && phys == 0xc000);
}

TEST(paging_unmap_4k) {
  Pool pool(64);
  PageTables pt;
  pt.init(&Pool::alloc, &pool, 0);
  CHECK(pt.map_4k(0x400000, 0x9000, arch::x86_64::kMapUser));
  CHECK(pt.unmap_4k(0x400000) == 0x9000);
  uint64_t phys = 0, flags = 0;
  CHECK(!pt.translate(0x400000, &phys, &flags));
  CHECK(pt.unmap_4k(0x400000) == 0);       // уже снято
  CHECK(pt.unmap_4k(0x7f0000000000ull) == 0);  // таблиц нет
  CHECK(pt.map_4k(0x400000, 0xa000, 0));    // слот снова свободен
}
