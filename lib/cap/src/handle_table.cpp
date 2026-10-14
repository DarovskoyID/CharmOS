#include <lib/cap/handle_table.hpp>

namespace lib::cap {

using lib::core::Result;

void HandleTable::init(Slot* slots, uint32_t count) {
  slots_ = slots;
  count_ = count > kMaxSlots ? kMaxSlots : count;
  used_ = 0;
  free_head_ = 0;
  // Список свободных по возрастанию индекса: 1, 2, …, count−1.
  for (uint32_t i = count_; i-- > 1;) {
    slots_[i] = {nullptr, 0, free_head_};
    free_head_ = i;
  }
  if (count_ > 0) slots_[0] = {nullptr, 0, 0};
}

Result<uint32_t, TableError> HandleTable::insert(void* object) {
  using R = Result<uint32_t, TableError>;
  if (free_head_ == 0) return R::err(TableError::Full);
  uint32_t i = free_head_;
  Slot& s = slots_[i];
  free_head_ = s.next_free;
  s.object = object;
  s.next_free = 0;
  ++used_;
  return R::ok(encode(i, s.generation));
}

bool HandleTable::decode(uint32_t handle, uint32_t* slot) const {
  uint32_t i = handle & (kMaxSlots - 1);
  if (i == 0 || i >= count_) return false;
  const Slot& s = slots_[i];
  if (s.object == nullptr || encode(i, s.generation) != handle) return false;
  *slot = i;
  return true;
}

void* HandleTable::lookup(uint32_t handle) const {
  uint32_t i;
  return decode(handle, &i) ? slots_[i].object : nullptr;
}

void* HandleTable::remove(uint32_t handle) {
  uint32_t i;
  if (!decode(handle, &i)) return nullptr;
  Slot& s = slots_[i];
  void* obj = s.object;
  s.object = nullptr;
  s.generation = (s.generation + 1) & kGenMask;
  s.next_free = free_head_;
  free_head_ = i;
  --used_;
  return obj;
}

uint32_t HandleTable::next_used(uint32_t* cursor) const {
  for (uint32_t i = *cursor < 1 ? 1 : *cursor; i < count_; ++i) {
    if (slots_[i].object != nullptr) {
      *cursor = i + 1;
      return encode(i, slots_[i].generation);
    }
  }
  *cursor = count_;
  return 0;
}

}  // namespace lib::cap
