#pragma once
#include <sys/time.h>
namespace libc { void run_finalizers(); }
extern "C" int __cxa_guard_acquire(uint64_t*);
extern "C" void __cxa_guard_release(uint64_t*);
extern "C" void __cxa_guard_abort(uint64_t*);
extern "C" int __cxa_atexit(void (*)(void*), void*, void*);
extern "C" void __cxa_finalize(void*);

namespace {
int cxx_init_order = 0;
unsigned cxx_local_created = 0, cxx_local_destroyed = 0;
void cxx_preinit() { CHECK(cxx_init_order == 0); cxx_init_order = 1; }
__attribute__((used, section(".preinit_array"))) void (*const cxx_preinit_entry)() = cxx_preinit;
struct PriorityProbe {
  explicit PriorityProbe(int expected) { CHECK(cxx_init_order == expected); ++cxx_init_order; }
};
__attribute__((init_priority(200))) PriorityProbe cxx_priority_first(1);
__attribute__((init_priority(300))) PriorityProbe cxx_priority_second(2);
struct GlobalProbe {
  int* memory;
  GlobalProbe() : memory(new int(42)) {
    CHECK(cxx_init_order == 3); ++cxx_init_order;
    printf("threads runtime: C++ preinit and global constructors ok\n");
  }
  ~GlobalProbe() {
    CHECK(*memory == 42 && cxx_local_destroyed == 1);
    delete memory;
    cxx_init_order = 5;
    printf("threads runtime: C++ global destructor ok\n");
  }
} cxx_global_probe;
void cxx_fini() {
  CHECK(cxx_init_order == 5);
  libc::run_finalizers();  // Reentrant finalization must not invoke this twice.
  printf("threads runtime: C++ fini array ok\n");
}
__attribute__((used, section(".fini_array"))) void (*const cxx_fini_entry)() = cxx_fini;
struct LocalProbe {
  int value;
  LocalProbe() : value(0) {
    __atomic_fetch_add(&cxx_local_created, 1u, __ATOMIC_RELAXED);
    charm::yield(); value = 77;
  }
  ~LocalProbe() { ++cxx_local_destroyed; }
};
LocalProbe& cxx_local() { static LocalProbe object; return object; }
void cxx_check_exit() {
  CHECK(cxx_local_created == 1 && cxx_local_destroyed == 1);
  printf("threads runtime: C++ local destructor and atexit order ok\n");
}
int cxx_callback_order = 0;
void cxx_callback(void* argument) {
  cxx_callback_order = cxx_callback_order * 10 + static_cast<int>(reinterpret_cast<uintptr_t>(argument));
}
unsigned cxx_aligned_destroyed = 0;
struct alignas(4096) AlignedProbe {
  uint64_t value = 123;
  ~AlignedProbe() { CHECK(value == 123); ++cxx_aligned_destroyed; }
};
void cxx_prepare() {
  timeval wall{};
  CHECK(gettimeofday(&wall, nullptr) == 0 && wall.tv_sec > 1700000000 && wall.tv_usec >= 0 && wall.tv_usec < 1000000);
  CHECK(gettimeofday(nullptr, nullptr) == -1 && errno == EFAULT);
  CHECK(cxx_init_order == 4 && *cxx_global_probe.memory == 42);
  auto* aligned = new AlignedProbe[3];
  CHECK(reinterpret_cast<uintptr_t>(aligned) % 4096 == 0);
  for (unsigned i = 0; i < 3; ++i) CHECK(aligned[i].value == 123);
  delete[] aligned;
  auto* scalar = new AlignedProbe;
  CHECK(reinterpret_cast<uintptr_t>(scalar) % 4096 == 0 && scalar->value == 123);
  delete scalar;
  CHECK(cxx_aligned_destroyed == 4);
  CHECK(malloc(static_cast<size_t>(-1)) == nullptr && errno == ENOMEM);
  uint64_t guard = 0;
  CHECK(__cxa_guard_acquire(&guard) == 1);
  __cxa_guard_abort(&guard);
  CHECK(__cxa_guard_acquire(&guard) == 1);
  __cxa_guard_release(&guard);
  CHECK(__cxa_guard_acquire(&guard) == 0);
  int dso_one, dso_two;
  CHECK(__cxa_atexit(cxx_callback, reinterpret_cast<void*>(1), &dso_one) == 0);
  CHECK(__cxa_atexit(cxx_callback, reinterpret_cast<void*>(2), &dso_two) == 0);
  CHECK(__cxa_atexit(cxx_callback, reinterpret_cast<void*>(3), &dso_one) == 0);
  __cxa_finalize(&dso_one); CHECK(cxx_callback_order == 31);
  __cxa_finalize(&dso_one); CHECK(cxx_callback_order == 31);
  __cxa_finalize(&dso_two); CHECK(cxx_callback_order == 312);
  CHECK(atexit(cxx_check_exit) == 0);
}
}
