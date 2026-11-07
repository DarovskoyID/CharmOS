#include <pthread.h>
#include "internal.hpp"

static_assert(sizeof(pthread_rwlock_t) == 40);
static_assert(sizeof(pthread_key_t) == 4);
namespace {
struct Key { uint32_t generation; bool active; void (*destructor)(void*); };
constinit Key g_keys[PTHREAD_KEYS_MAX] = {};
constinit libc::Lock g_keys_lock{};
constexpr uint32_t kSlotBits = 7;
constexpr uint32_t kSlotMask = 127;
bool valid_key(pthread_key_t key) {
  const auto& entry = g_keys[key & kSlotMask];
  return entry.active && entry.generation == (key >> kSlotBits);
}
int read_locked(pthread_rwlock_t* lock) {
  if (lock->readers == UINT32_MAX) return EAGAIN;
  ++lock->readers;
  return 0;
}
}

namespace libc {
void thread_specific_cleanup() {
  auto* thread = current_thread();
  for (unsigned pass = 0; pass < PTHREAD_DESTRUCTOR_ITERATIONS; ++pass) {
    bool called = false;
    for (unsigned slot = 0; slot < PTHREAD_KEYS_MAX; ++slot) {
      void (*destructor)(void*) = nullptr;
      void* value = nullptr;
      {
        Guard guard(g_keys_lock);
        auto& local = thread->specific[slot];
        const auto& key = g_keys[slot];
        if (key.active && local.generation == key.generation && local.value && key.destructor) {
          destructor = key.destructor;
          value = local.value;
          local.value = nullptr;
        }
      }
      if (destructor) { called = true; destructor(value); }
    }
    if (!called) break;
  }
}
}

extern "C" {
int pthread_key_create(pthread_key_t* out, void (*destructor)(void*)) {
  if (!out) return EINVAL;
  libc::Guard guard(g_keys_lock);
  for (uint32_t slot = 0; slot < PTHREAD_KEYS_MAX; ++slot) {
    auto& key = g_keys[slot];
    if (key.active || key.generation == (UINT32_MAX >> kSlotBits)) continue;
    ++key.generation;
    key.active = true;
    key.destructor = destructor;
    *out = (key.generation << kSlotBits) | slot;
    return 0;
  }
  return EAGAIN;
}
int pthread_key_delete(pthread_key_t key) {
  libc::Guard guard(g_keys_lock);
  if (!valid_key(key)) return EINVAL;
  g_keys[key & kSlotMask].active = false;
  g_keys[key & kSlotMask].destructor = nullptr;
  return 0;
}
int pthread_setspecific(pthread_key_t key, const void* value) {
  libc::Guard guard(g_keys_lock);
  if (!valid_key(key)) return EINVAL;
  auto& local = libc::current_thread()->specific[key & kSlotMask];
  local.generation = key >> kSlotBits;
  local.value = const_cast<void*>(value);
  return 0;
}
void* pthread_getspecific(pthread_key_t key) {
  libc::Guard guard(g_keys_lock);
  if (!valid_key(key)) return nullptr;
  const auto& local = libc::current_thread()->specific[key & kSlotMask];
  return local.generation == (key >> kSlotBits) ? local.value : nullptr;
}
int pthread_rwlock_init(pthread_rwlock_t* lock, const pthread_rwlockattr_t* attr) {
  if (attr) return EINVAL;
  *lock = PTHREAD_RWLOCK_INITIALIZER;
  return 0;
}
int pthread_rwlock_destroy(pthread_rwlock_t* lock) {
  if (pthread_mutex_trylock(&lock->gate)) return EBUSY;
  int result = lock->writer || lock->readers ? EBUSY : 0;
  pthread_mutex_unlock(&lock->gate);
  return result;
}
int pthread_rwlock_rdlock(pthread_rwlock_t* lock) {
  pthread_mutex_lock(&lock->gate);
  while (lock->writer) pthread_cond_wait(&lock->changed, &lock->gate);
  int result = read_locked(lock);
  pthread_mutex_unlock(&lock->gate);
  return result;
}
int pthread_rwlock_wrlock(pthread_rwlock_t* lock) {
  pthread_mutex_lock(&lock->gate);
  while (lock->writer || lock->readers) pthread_cond_wait(&lock->changed, &lock->gate);
  lock->writer = 1;
  pthread_mutex_unlock(&lock->gate);
  return 0;
}
int pthread_rwlock_tryrdlock(pthread_rwlock_t* lock) {
  if (pthread_mutex_trylock(&lock->gate)) return EBUSY;
  int result = lock->writer ? EBUSY : read_locked(lock);
  pthread_mutex_unlock(&lock->gate);
  return result;
}
int pthread_rwlock_trywrlock(pthread_rwlock_t* lock) {
  if (pthread_mutex_trylock(&lock->gate)) return EBUSY;
  int result = lock->writer || lock->readers ? EBUSY : 0;
  if (!result) lock->writer = 1;
  pthread_mutex_unlock(&lock->gate);
  return result;
}
int pthread_rwlock_unlock(pthread_rwlock_t* lock) {
  pthread_mutex_lock(&lock->gate);
  int result = 0;
  if (lock->writer) lock->writer = 0;
  else if (lock->readers) --lock->readers;
  else result = EPERM;
  if (!lock->writer && !lock->readers) pthread_cond_broadcast(&lock->changed);
  pthread_mutex_unlock(&lock->gate);
  return result;
}

int pthread_setname_np(pthread_t, const char*) {
  // The kernel has no thread-name operation; do not report a change.
  return ENOTSUP;
}
}
