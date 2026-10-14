#pragma once
// Аллокатор физических страниц (фреймов) на битовой карте.
// Бит 1 — фрейм свободен, 0 — занят. Память под карту даёт вызывающий.
// Не знает ни о железе, ни о ядре: тестируется на хосте.

#include <stddef.h>
#include <stdint.h>

#include <lib/core/result.hpp>

namespace lib::mm {

inline constexpr uint64_t kFrameSize = 4096;

enum class FrameError : uint8_t {
  OutOfMemory,
  InvalidArgument,
  DoubleFree,
};

class FrameBitmap {
 public:
  // Сколько 64-битных слов нужно под карту для frame_count фреймов.
  static constexpr size_t words_for(uint64_t frame_count) {
    return static_cast<size_t>((frame_count + 63) / 64);
  }

  // words — words_for(frame_count) слов. Изначально все фреймы заняты.
  void init(uint64_t* words, uint64_t frame_count);

  // Помечает фреймы, целиком лежащие в [base, base+length), свободными/занятыми.
  void mark_free(uint64_t base, uint64_t length);
  void mark_used(uint64_t base, uint64_t length);

  // Выделяет count подряд идущих фреймов, первый из которых выровнен на
  // align_frames (степень двойки). Возвращает физический адрес первого.
  lib::core::Result<uint64_t, FrameError> alloc(uint64_t count = 1, uint64_t align_frames = 1);

  // То же, но весь участок обязан лежать ниже limit_phys (физ. адрес, не включительно).
  // Нужно, например, для кода запуска AP, который исполняется в реальном режиме.
  lib::core::Result<uint64_t, FrameError> alloc_below(uint64_t limit_phys, uint64_t count = 1,
                                                      uint64_t align_frames = 1);

  // Освобождает count фреймов с адреса phys. Освобождение свободного фрейма —
  // DoubleFree, фреймы при этом не меняются.
  lib::core::Status<FrameError> free(uint64_t phys, uint64_t count = 1);

  bool is_free(uint64_t phys) const;
  uint64_t free_frames() const { return free_count_; }
  uint64_t frame_count() const { return frame_count_; }

 private:
  bool test(uint64_t frame) const { return (words_[frame / 64] >> (frame % 64)) & 1; }
  void set(uint64_t frame) { words_[frame / 64] |= 1ull << (frame % 64); }
  void clear(uint64_t frame) { words_[frame / 64] &= ~(1ull << (frame % 64)); }

  uint64_t* words_ = nullptr;
  uint64_t frame_count_ = 0;
  uint64_t free_count_ = 0;
  uint64_t hint_ = 0;  // слово, с которого начинать поиск одиночных фреймов
};

}  // namespace lib::mm
