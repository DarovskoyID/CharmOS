#pragma once
#include <stdint.h>

namespace lib::input {
// Empty-to-nonempty notification handshake. Both sides must use these paired
// store/load operations before sleeping. SC prevents both from seeing the old
// counter when their operations race; the reader must drain again on false.
inline bool publish_tail(uint32_t* tail, const uint32_t* head, uint32_t next) {
  __atomic_store_n(tail, next, __ATOMIC_SEQ_CST);
  return __atomic_load_n(head, __ATOMIC_SEQ_CST) == next - 1;
}
inline bool consume_head(uint32_t* head, const uint32_t* tail, uint32_t next) {
  __atomic_store_n(head, next, __ATOMIC_SEQ_CST);
  return __atomic_load_n(tail, __ATOMIC_SEQ_CST) == next;
}
}  // namespace lib::input
