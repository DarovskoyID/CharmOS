#include <cstdlib>

#include <lib/mm/frame_bitmap.hpp>
#include <lib/mm/heap.hpp>

#include "test.hpp"

using lib::mm::FrameBitmap;
using lib::mm::FrameError;
using lib::mm::Heap;
using lib::mm::HeapError;
using lib::mm::kFrameSize;

namespace {

// Детерминированный ГПСЧ (xorshift64) для стресс-тестов.
struct Rng {
  uint64_t s = 0x9e3779b97f4a7c15ull;
  uint64_t next() {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
  }
  uint64_t below(uint64_t n) { return next() % n; }
};

struct Frames {
  uint64_t words[FrameBitmap::words_for(1000)];
  FrameBitmap fb;
  Frames() {
    fb.init(words, 1000);
    fb.mark_free(0, 1000 * kFrameSize);
  }
};

}  // namespace

TEST(frames_init_all_used_then_mark) {
  uint64_t words[FrameBitmap::words_for(100)];
  FrameBitmap fb;
  fb.init(words, 100);
  CHECK(fb.free_frames() == 0);
  CHECK(fb.alloc().error() == FrameError::OutOfMemory);
  fb.mark_free(0x1800, 0x3000);  // частичные фреймы не считаются
  CHECK(fb.free_frames() == 2);
  CHECK(fb.is_free(0x2000) && fb.is_free(0x3000) && !fb.is_free(0x1000));
  fb.mark_used(0x2fff, 2);  // задевает фреймы 2 и 3
  CHECK(fb.free_frames() == 0);
  fb.mark_free(0, 1000 * kFrameSize);  // за краем карты — обрезается
  CHECK(fb.free_frames() == 100);
}

TEST(frames_alloc_free_cycle) {
  Frames f;
  uint64_t got[1000];
  for (int i = 0; i < 1000; ++i) {
    auto r = f.fb.alloc();
    CHECK(r.is_ok());
    got[i] = r.value();
  }
  CHECK(f.fb.free_frames() == 0);
  CHECK(f.fb.alloc().is_err());
  for (int i = 0; i < 1000; ++i) CHECK(f.fb.free(got[i]).is_ok());
  CHECK(f.fb.free_frames() == 1000);
}

TEST(frames_unique_addresses) {
  Frames f;
  bool seen[1000] = {};
  for (int i = 0; i < 1000; ++i) {
    uint64_t a = f.fb.alloc().value();
    CHECK(a % kFrameSize == 0);
    size_t idx = a / kFrameSize;
    CHECK(!seen[idx]);
    seen[idx] = true;
  }
}

TEST(frames_contiguous_and_aligned) {
  Frames f;
  f.fb.mark_used(5 * kFrameSize, kFrameSize);  // дырка в начале
  auto r = f.fb.alloc(8, 8);
  CHECK(r.is_ok() && r.value() == 8 * kFrameSize);
  auto r2 = f.fb.alloc(3);
  CHECK(r2.is_ok() && r2.value() == 0);
  auto big = f.fb.alloc(2000);
  CHECK(big.error() == FrameError::OutOfMemory);
  CHECK(f.fb.alloc(1, 3).error() == FrameError::InvalidArgument);
  CHECK(f.fb.alloc(0).error() == FrameError::InvalidArgument);
}

TEST(frames_double_free_and_bad_args) {
  Frames f;
  uint64_t a = f.fb.alloc().value();
  CHECK(f.fb.free(a).is_ok());
  CHECK(f.fb.free(a).error() == FrameError::DoubleFree);
  CHECK(f.fb.free(a + 1).error() == FrameError::InvalidArgument);
  CHECK(f.fb.free(5000 * kFrameSize).error() == FrameError::InvalidArgument);
}

// --- куча --------------------------------------------------------------------

namespace {

struct Arena {
  static constexpr size_t kMax = 4 * 1024 * 1024;
  uint8_t* mem;
  size_t grows = 0;
  bool refuse = false;
  Arena() : mem(static_cast<uint8_t*>(std::aligned_alloc(4096, kMax))) {}
  ~Arena() { std::free(mem); }
  static bool grow(void* ctx, uintptr_t, size_t) {
    auto* a = static_cast<Arena*>(ctx);
    if (a->refuse) return false;
    ++a->grows;
    return true;
  }
  uintptr_t base() const { return reinterpret_cast<uintptr_t>(mem); }
};

}  // namespace

TEST(heap_init_validation) {
  Arena a;
  Heap h;
  CHECK(h.init(a.base() + 8, Arena::kMax, &Arena::grow, &a).error() ==
        HeapError::InvalidArgument);
  CHECK(h.init(a.base(), 100, &Arena::grow, &a).error() == HeapError::InvalidArgument);
  a.refuse = true;
  CHECK(h.init(a.base(), Arena::kMax, &Arena::grow, &a).error() == HeapError::OutOfMemory);
}

TEST(heap_basic_alloc_free) {
  Arena a;
  Heap h;
  CHECK(h.init(a.base(), Arena::kMax, &Arena::grow, &a).is_ok());
  CHECK(h.check());
  void* p = h.alloc(10).value();
  void* q = h.alloc(100).value();
  CHECK(reinterpret_cast<uintptr_t>(p) % 16 == 0);
  CHECK(reinterpret_cast<uintptr_t>(q) % 16 == 0);
  CHECK(p != q);
  std::memset(p, 0xaa, 10);
  std::memset(q, 0xbb, 100);
  CHECK(h.check());
  CHECK(h.free(p).is_ok());
  CHECK(h.free(q).is_ok());
  CHECK(h.check());
  CHECK(h.used_bytes() == 0);
  CHECK(h.free_list_length() == 1);  // всё слилось обратно
}

TEST(heap_bad_frees) {
  Arena a;
  Heap h;
  CHECK(h.init(a.base(), Arena::kMax, &Arena::grow, &a).is_ok());
  void* p = h.alloc(64).value();
  CHECK(h.free(static_cast<uint8_t*>(p) + 16).error() == HeapError::InvalidPointer);
  CHECK(h.free(reinterpret_cast<void*>(0x10)).error() == HeapError::InvalidPointer);
  CHECK(h.free(p).is_ok());
  CHECK(h.free(p).error() == HeapError::InvalidPointer);  // двойное освобождение
  void* r = h.alloc(64).value();
  // Порча концевика: запись за конец выделения.
  std::memset(r, 0, 64 + 8);
  CHECK(h.free(r).error() == HeapError::Corrupted);
}

TEST(heap_grows_and_reports_oom) {
  Arena a;
  Heap h;
  CHECK(h.init(a.base(), 256 * 1024, &Arena::grow, &a).is_ok());
  void* big = h.alloc(100 * 1024).value();
  CHECK(a.grows >= 2);
  CHECK(h.check());
  CHECK(h.alloc(512 * 1024).error() == HeapError::OutOfMemory);
  CHECK(h.free(big).is_ok());
  CHECK(h.check());
}

TEST(heap_random_stress) {
  Arena a;
  Heap h;
  CHECK(h.init(a.base(), Arena::kMax, &Arena::grow, &a).is_ok());
  Rng rng;
  constexpr int kSlots = 256;
  uint8_t* ptr[kSlots] = {};
  size_t len[kSlots] = {};
  for (int step = 0; step < 20000; ++step) {
    int i = static_cast<int>(rng.below(kSlots));
    if (ptr[i] == nullptr) {
      size_t n = 1 + rng.below(rng.below(8) == 0 ? 8192 : 256);
      auto r = h.alloc(n);
      if (r.is_err()) continue;
      ptr[i] = static_cast<uint8_t*>(r.value());
      len[i] = n;
      std::memset(ptr[i], i, n);
    } else {
      bool intact = true;
      for (size_t k = 0; k < len[i]; ++k) intact = intact && ptr[i][k] == static_cast<uint8_t>(i);
      CHECK(intact);  // чужое выделение не перезаписало данные
      if (!intact) return;
      CHECK(h.free(ptr[i]).is_ok());
      ptr[i] = nullptr;
    }
    if (step % 1000 == 0) CHECK(h.check());
  }
  for (int i = 0; i < kSlots; ++i) {
    if (ptr[i] != nullptr) CHECK(h.free(ptr[i]).is_ok());
  }
  CHECK(h.check());
  CHECK(h.used_bytes() == 0);
  CHECK(h.free_list_length() == 1);
}

TEST(frames_alloc_below_limit) {
  Frames f;
  f.fb.mark_used(0, 4 * kFrameSize);  // первые 4 фрейма заняты
  auto low = f.fb.alloc_below(8 * kFrameSize);
  CHECK(low.is_ok() && low.value() == 4 * kFrameSize);
  auto two = f.fb.alloc_below(8 * kFrameSize, 2);
  CHECK(two.is_ok() && two.value() == 5 * kFrameSize);
  CHECK(f.fb.alloc_below(8 * kFrameSize, 2).is_err());  // остался один фрейм ниже предела
  CHECK(f.fb.alloc_below(8 * kFrameSize, 1).value() == 7 * kFrameSize);
  CHECK(f.fb.alloc_below(8 * kFrameSize).error() == FrameError::OutOfMemory);
  CHECK(f.fb.alloc().is_ok());  // выше предела память есть
}
