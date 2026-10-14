#include <lib/cap/handle_table.hpp>

#include "test.hpp"

using lib::cap::HandleTable;
using lib::cap::TableError;

namespace {

struct Table {
  HandleTable::Slot slots[8];
  HandleTable t;
  Table() { t.init(slots, 8); }
};

int a, b, c;

}  // namespace

TEST(handles_insert_lookup_remove) {
  Table tb;
  CHECK(tb.t.capacity() == 7);
  uint32_t ha = tb.t.insert(&a).value();
  uint32_t hb = tb.t.insert(&b).value();
  CHECK(ha != 0 && hb != 0 && ha != hb);
  CHECK(tb.t.lookup(ha) == &a && tb.t.lookup(hb) == &b);
  CHECK(tb.t.remove(ha) == &a);
  CHECK(tb.t.lookup(ha) == nullptr);
  CHECK(tb.t.remove(ha) == nullptr);  // повторное закрытие
  CHECK(tb.t.used() == 1);
}

TEST(handles_stale_value_after_slot_reuse) {
  Table tb;
  uint32_t old = tb.t.insert(&a).value();
  tb.t.remove(old);
  uint32_t fresh = tb.t.insert(&c).value();  // тот же слот (LIFO), новое поколение
  CHECK((fresh & (HandleTable::kMaxSlots - 1)) == (old & (HandleTable::kMaxSlots - 1)));
  CHECK(fresh != old);
  CHECK(tb.t.lookup(old) == nullptr);
  CHECK(tb.t.lookup(fresh) == &c);
}

TEST(handles_forged_values_rejected) {
  Table tb;
  uint32_t h = tb.t.insert(&a).value();
  CHECK(tb.t.lookup(0) == nullptr);
  CHECK(tb.t.lookup(h ^ (1u << 10)) == nullptr);  // чужое поколение
  CHECK(tb.t.lookup(h + 1) == nullptr);           // свободный слот
  CHECK(tb.t.lookup(7) == nullptr);
  CHECK(tb.t.lookup(100) == nullptr);             // за пределами таблицы
  CHECK(tb.t.lookup(0xffffffffu) == nullptr);
}

TEST(handles_table_full_and_iteration) {
  Table tb;
  uint32_t hs[7];
  for (int i = 0; i < 7; ++i) hs[i] = tb.t.insert(&a).value();
  CHECK(tb.t.insert(&b).error() == TableError::Full);
  tb.t.remove(hs[3]);
  uint32_t cursor = 0, n = 0;
  while (tb.t.next_used(&cursor) != 0) ++n;
  CHECK(n == 6);
  CHECK(tb.t.insert(&b).is_ok());
}
