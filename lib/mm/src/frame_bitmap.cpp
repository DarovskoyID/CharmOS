#include <lib/mm/frame_bitmap.hpp>

namespace lib::mm {

using lib::core::Result;
using lib::core::Status;

void FrameBitmap::init(uint64_t* words, uint64_t frame_count) {
  words_ = words;
  frame_count_ = frame_count;
  free_count_ = 0;
  hint_ = 0;
  for (size_t i = 0; i < words_for(frame_count); ++i) words_[i] = 0;
}

void FrameBitmap::mark_free(uint64_t base, uint64_t length) {
  uint64_t first = (base + kFrameSize - 1) / kFrameSize;
  uint64_t last = (base + length) / kFrameSize;  // не включительно
  if (last > frame_count_) last = frame_count_;
  for (uint64_t f = first; f < last; ++f) {
    if (!test(f)) {
      set(f);
      ++free_count_;
    }
  }
}

void FrameBitmap::mark_used(uint64_t base, uint64_t length) {
  uint64_t first = base / kFrameSize;
  uint64_t last = (base + length + kFrameSize - 1) / kFrameSize;
  if (last > frame_count_) last = frame_count_;
  for (uint64_t f = first; f < last; ++f) {
    if (test(f)) {
      clear(f);
      --free_count_;
    }
  }
}

Result<uint64_t, FrameError> FrameBitmap::alloc(uint64_t count, uint64_t align_frames) {
  // Сначала выше preferred_start_ (память ниже дефицитна: код запуска AP и т.п.).
  if (preferred_start_ != 0) {
    auto r = alloc_from(preferred_start_, frame_count_, count, align_frames);
    if (r.is_ok() || r.error() != FrameError::OutOfMemory) return r;
  }
  return alloc_from(0, frame_count_, count, align_frames);
}

void FrameBitmap::set_preferred_start(uint64_t phys) { preferred_start_ = phys / kFrameSize; }

Result<uint64_t, FrameError> FrameBitmap::alloc_below(uint64_t limit_phys, uint64_t count,
                                                      uint64_t align_frames) {
  uint64_t limit = limit_phys / kFrameSize;
  return alloc_from(0, limit > frame_count_ ? frame_count_ : limit, count, align_frames);
}

Result<uint64_t, FrameError> FrameBitmap::alloc_from(uint64_t start_frame, uint64_t limit,
                                                     uint64_t count, uint64_t align_frames) {
  using R = Result<uint64_t, FrameError>;
  if (count == 0 || align_frames == 0 || (align_frames & (align_frames - 1)) != 0) {
    return R::err(FrameError::InvalidArgument);
  }
  if (count > free_count_) return R::err(FrameError::OutOfMemory);
  size_t words = words_for(frame_count_);
  if (count == 1 && align_frames == 1 && limit == frame_count_) {
    // Быстрый путь: первое ненулевое слово начиная с подсказки (не ниже start).
    size_t first = static_cast<size_t>(start_frame / 64);
    size_t span = words - first;
    size_t from = hint_ >= first ? hint_ : first;
    for (size_t k = 0; k < span; ++k) {
      size_t w = first + (from - first + k) % span;
      if (words_[w] == 0) continue;
      uint64_t bit = static_cast<uint64_t>(__builtin_ctzll(words_[w]));
      uint64_t f = w * 64 + bit;
      if (f >= frame_count_ || f < start_frame) continue;
      clear(f);
      --free_count_;
      hint_ = w;
      return R::ok(f * kFrameSize);
    }
    return R::err(FrameError::OutOfMemory);
  }

  // Непрерывный участок: линейный поиск по выровненным стартам.
  uint64_t first_start = (start_frame + align_frames - 1) / align_frames * align_frames;
  for (uint64_t start = first_start; start + count <= limit; start += align_frames) {
    uint64_t run = 0;
    while (run < count && test(start + run)) ++run;
    if (run == count) {
      for (uint64_t i = 0; i < count; ++i) clear(start + i);
      free_count_ -= count;
      return R::ok(start * kFrameSize);
    }
    // Перепрыгиваем занятый фрейм, сохраняя выравнивание.
    uint64_t skip_to = start + run + 1;
    start = (skip_to + align_frames - 1) / align_frames * align_frames - align_frames;
  }
  return R::err(FrameError::OutOfMemory);
}

Status<FrameError> FrameBitmap::free(uint64_t phys, uint64_t count) {
  using S = Status<FrameError>;
  if (phys % kFrameSize != 0 || count == 0) return S::err(FrameError::InvalidArgument);
  uint64_t first = phys / kFrameSize;
  if (first >= frame_count_ || count > frame_count_ - first) {
    return S::err(FrameError::InvalidArgument);
  }
  for (uint64_t f = first; f < first + count; ++f) {
    if (test(f)) return S::err(FrameError::DoubleFree);
  }
  for (uint64_t f = first; f < first + count; ++f) set(f);
  free_count_ += count;
  return lib::core::ok_status<FrameError>();
}

bool FrameBitmap::is_free(uint64_t phys) const {
  uint64_t f = phys / kFrameSize;
  return f < frame_count_ && test(f);
}

}  // namespace lib::mm
