#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "internal.hpp"

namespace std {
struct nothrow_t { explicit nothrow_t() = default; };
extern const nothrow_t nothrow;
const nothrow_t nothrow{};
enum class align_val_t : size_t {};
}

extern "C" {
void* __dso_handle = &__dso_handle;
extern void (*__preinit_array_start[])();
extern void (*__preinit_array_end[])();
extern void (*__init_array_start[])();
extern void (*__init_array_end[])();
extern void (*__fini_array_start[])();
extern void (*__fini_array_end[])();
}
namespace {
using GuardWord = uint32_t __attribute__((may_alias));
struct ExitEntry {
  ExitEntry* next;
  void (*plain)();
  void (*object)(void*);
  void* argument;
  void* dso;
};
constinit libc::Lock exit_lock{};
ExitEntry* exit_head = nullptr;
size_t exit_count = 0;
uint32_t finalizers_started = 0;
int register_handler(void (*plain)(), void (*object)(void*), void* argument, void* dso) {
  if (!plain && !object) return -1;
  auto* entry = static_cast<ExitEntry*>(malloc(sizeof(ExitEntry)));
  if (!entry) return -1;
  exit_lock.lock();
  if (exit_count == 4096) { exit_lock.unlock(); free(entry); return -1; }
  *entry = {exit_head, plain, object, argument, dso};
  exit_head = entry; ++exit_count;
  exit_lock.unlock();
  return 0;
}
void* allocate(size_t size, bool fatal) {
  void* memory = malloc(size ? size : 1);
  if (!memory && fatal) abort();
  return memory;
}
void* allocate_aligned(size_t size, size_t alignment, bool fatal) {
  if (!alignment || (alignment & (alignment - 1))) {
    if (fatal) abort();
    return nullptr;
  }
  if (alignment < alignof(void*)) alignment = alignof(void*);
  if (size > SIZE_MAX - sizeof(void*) - (alignment - 1)) {
    if (fatal) abort();
    return nullptr;
  }
  void* base = allocate((size ? size : 1) + sizeof(void*) + alignment - 1, fatal);
  if (!base) return nullptr;
  uintptr_t address = (reinterpret_cast<uintptr_t>(base) + sizeof(void*) + alignment - 1) & ~(alignment - 1);
  auto** aligned = reinterpret_cast<void**>(address);
  aligned[-1] = base;
  return aligned;
}
void free_aligned(void* memory) { if (memory) free(static_cast<void**>(memory)[-1]); }
}
extern "C" int __cxa_atexit(void (*fn)(void*), void* object, void* dso) {
  return register_handler(nullptr, fn, object, dso);
}
extern "C" void __cxa_finalize(void* dso) {
  for (;;) {
    exit_lock.lock();
    ExitEntry** link = &exit_head;
    while (*link && dso && (*link)->dso != dso) link = &(*link)->next;
    ExitEntry* entry = *link;
    if (!entry) { exit_lock.unlock(); return; }
    *link = entry->next; --exit_count;
    exit_lock.unlock();
    auto plain = entry->plain;
    auto object = entry->object;
    void* argument = entry->argument;
    free(entry);
    if (plain) plain(); else object(argument);
  }
}
namespace libc {
int register_exit(void (*fn)()) { return register_handler(fn, nullptr, nullptr, &__dso_handle); }
void run_exit_handlers() { __cxa_finalize(nullptr); }
void run_initializers() {
  for (auto p = __preinit_array_start; p != __preinit_array_end; ++p) (*p)();
  for (auto p = __init_array_start; p != __init_array_end; ++p) (*p)();
}
void run_finalizers() {
  if (__atomic_exchange_n(&finalizers_started, 1u, __ATOMIC_ACQ_REL)) return;
  for (auto p = __fini_array_end; p != __fini_array_start;) (*--p)();
}
}
extern "C" int __cxa_guard_acquire(uint64_t* guard) {
  auto* complete = reinterpret_cast<uint8_t*>(guard);
  auto* pending = reinterpret_cast<GuardWord*>(guard) + 1;
  for (;;) {
    if (__atomic_load_n(complete, __ATOMIC_ACQUIRE)) return 0;
    uint32_t expected = 0;
    if (__atomic_compare_exchange_n(pending, &expected, 1u, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
      if (!__atomic_load_n(complete, __ATOMIC_ACQUIRE)) return 1;
      __atomic_store_n(pending, 0u, __ATOMIC_RELEASE);
      (void)charm::futex_wake(pending, ~uint64_t{0});
      return 0;
    }
    (void)charm::futex_wait(pending, 1u, charm::kForever);
  }
}
extern "C" void __cxa_guard_release(uint64_t* guard) {
  __atomic_store_n(reinterpret_cast<uint8_t*>(guard), uint8_t{1}, __ATOMIC_RELEASE);
  auto* pending = reinterpret_cast<GuardWord*>(guard) + 1;
  __atomic_store_n(pending, 0u, __ATOMIC_RELEASE);
  (void)charm::futex_wake(pending, ~uint64_t{0});
}
extern "C" void __cxa_guard_abort(uint64_t* guard) {
  auto* pending = reinterpret_cast<GuardWord*>(guard) + 1;
  __atomic_store_n(pending, 0u, __ATOMIC_RELEASE);
  (void)charm::futex_wake(pending, ~uint64_t{0});
}
extern "C" [[noreturn]] void __cxa_pure_virtual() { abort(); }
extern "C" [[noreturn]] void __cxa_deleted_virtual() { abort(); }

void* operator new(size_t n) { return allocate(n, true); }
void* operator new[](size_t n) { return allocate(n, true); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { return allocate(n, false); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return allocate(n, false); }
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { free(p); }
void* operator new(size_t n, std::align_val_t a) { return allocate_aligned(n, static_cast<size_t>(a), true); }
void* operator new[](size_t n, std::align_val_t a) { return allocate_aligned(n, static_cast<size_t>(a), true); }
void* operator new(size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return allocate_aligned(n, static_cast<size_t>(a), false); }
void* operator new[](size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return allocate_aligned(n, static_cast<size_t>(a), false); }
void operator delete(void* p, std::align_val_t) noexcept { free_aligned(p); }
void operator delete[](void* p, std::align_val_t) noexcept { free_aligned(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { free_aligned(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { free_aligned(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { free_aligned(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { free_aligned(p); }
