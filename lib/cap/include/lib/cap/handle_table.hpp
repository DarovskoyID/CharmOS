#pragma once
// Таблица хендлов процесса (docs/abi/ipc.md §2): слот → указатель на
// капабилити ядра. Значение хендла = (поколение << 10) | слот; слот 0 не
// выдаётся, поэтому хендл никогда не равен 0. Поколение слота растёт при
// каждом освобождении: старое значение хендла не начинает указывать на новый
// объект, даже если слот переиспользован.
//
// Без блокировок и без выделения памяти — синхронизирует и выделяет вызывающий.

#include <stddef.h>
#include <stdint.h>

#include <lib/core/result.hpp>

namespace lib::cap {

enum class TableError : uint8_t { Full };

class HandleTable {
 public:
  static constexpr uint32_t kSlotBits = 10;
  static constexpr uint32_t kMaxSlots = 1u << kSlotBits;  // включая неиспользуемый слот 0
  static constexpr uint32_t kGenMask = (1u << (32 - kSlotBits)) - 1;

  struct Slot {
    void* object;  // nullptr — свободен
    uint32_t generation;
    uint32_t next_free;  // индекс следующего свободного (0 — конец списка)
  };

  // slots — массив из count элементов (count ≤ kMaxSlots). Ёмкость — count − 1.
  void init(Slot* slots, uint32_t count);

  lib::core::Result<uint32_t, TableError> insert(void* object);
  // nullptr, если значение не указывает на занятый слот текущего поколения.
  void* lookup(uint32_t handle) const;
  // Освобождает слот; возвращает объект или nullptr, если хендл недействителен.
  void* remove(uint32_t handle);

  uint32_t used() const { return used_; }
  uint32_t capacity() const { return count_ == 0 ? 0 : count_ - 1; }

  // Обход занятых слотов (для закрытия всех хендлов при выходе процесса).
  // Возвращает значение хендла занятого слота с индексом ≥ *cursor и сдвигает
  // курсор; 0 — больше нет.
  uint32_t next_used(uint32_t* cursor) const;

 private:
  static constexpr uint32_t encode(uint32_t slot, uint32_t gen) {
    return ((gen & kGenMask) << kSlotBits) | slot;
  }
  bool decode(uint32_t handle, uint32_t* slot) const;

  Slot* slots_ = nullptr;
  uint32_t count_ = 0;
  uint32_t free_head_ = 0;
  uint32_t used_ = 0;
};

}  // namespace lib::cap
