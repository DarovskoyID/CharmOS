// pthread поверх потоков ядра и futex (docs/abi/process.md §4).
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "internal.hpp"
extern "C" int fegetenv(fenv_t*) __attribute__((weak));
extern "C" int fesetenv(const fenv_t*) __attribute__((weak));

struct __pthread : libc::ThreadBlock {};
static_assert(sizeof(pthread_mutex_t) == 24 && __builtin_offsetof(pthread_mutex_t, owner) == 8);

namespace libc {
namespace {

constexpr uint64_t kStack = 256 * 1024;

__attribute__((force_align_arg_pointer)) void thread_entry(uint64_t arg) {
  auto* t = reinterpret_cast<ThreadBlock*>(arg);
  (void)charm::thread_set_fs(arg);
  if(t->has_initial_fenv) (void)fesetenv(&t->initial_fenv);
  t->ret = t->fn(t->arg);
  thread_specific_cleanup();
  __atomic_store_n(&t->done, 1u, __ATOMIC_RELEASE);
  charm::thread_exit(0);
}

}  // namespace
}  // namespace libc

using namespace libc;

extern "C" {

int pthread_create(pthread_t* out, const pthread_attr_t*, void* (*fn)(void*), void* arg) {
  auto* t = static_cast<__pthread*>(calloc(1, sizeof(__pthread)));
  if (t == nullptr) return EAGAIN;
  auto v = charm::vmo_create(kStack);
  if (!v.ok()) {
    free(t);
    return EAGAIN;
  }
  auto m = charm::vmo_map(static_cast<Handle>(v.value), charm::rights::kRead | charm::rights::kWrite);
  (void)charm::handle_close(static_cast<Handle>(v.value));
  if (!m.ok()) {
    free(t);
    return EAGAIN;
  }
  t->self = t;
  t->fn = fn;
  t->arg = arg;
  t->stack_top = m.value + kStack;
  t->has_initial_fenv=fegetenv && fesetenv && fegetenv(&t->initial_fenv)==0;
  auto r = charm::thread_create(&thread_entry, t->stack_top, reinterpret_cast<uint64_t>(t));
  if (!r.ok()) {
    (void)charm::vmo_unmap(m.value);
    free(t);
    return r.status == Status::NoSpace ? EAGAIN : errno_from(r.status);
  }
  t->handle = static_cast<Handle>(r.value);
  *out = t;
  return 0;
}

int pthread_join(pthread_t t, void** ret) {
  // Ждём пакет ThreadExited: после него стек потока свободен.
  auto port = charm::port_create();
  if (!port.ok()) return EAGAIN;
  Handle p = static_cast<Handle>(port.value);
  if (charm::object_bind(p, t->handle, 1) == Status::Ok) {
    charm::PortPacket pkt;
    while (charm::port_wait(p, &pkt, charm::kForever) == Status::Interrupted) {
    }
  }
  (void)charm::handle_close(p);
  if (ret != nullptr) *ret = t->ret;
  (void)charm::handle_close(t->handle);
  (void)charm::vmo_unmap(t->stack_top - kStack);
  free(t);
  return 0;
}

int pthread_detach(pthread_t t) {
  t->detached = true;  // стек и блок остаются (правило 9: освобождение — по потребности)
  return 0;
}

pthread_t pthread_self(void) { return static_cast<pthread_t>(current_thread()); }

void pthread_exit(void* ret) {
  ThreadBlock* t = current_thread();
  t->ret = ret;
  thread_specific_cleanup();
  __atomic_store_n(&t->done, 1u, __ATOMIC_RELEASE);
  charm::thread_exit(0);
}

int sched_yield(void) { charm::yield(); return 0; }
int pthread_mutexattr_init(pthread_mutexattr_t* a) { a->type = PTHREAD_MUTEX_NORMAL; return 0; }
int pthread_mutexattr_destroy(pthread_mutexattr_t*) { return 0; }
int pthread_mutexattr_settype(pthread_mutexattr_t* a, int type) {
  if (type != PTHREAD_MUTEX_NORMAL && type != PTHREAD_MUTEX_RECURSIVE) return EINVAL;
  a->type = type; return 0;
}
int pthread_mutex_init(pthread_mutex_t* m, const pthread_mutexattr_t* a) {
  *m = {0, a ? a->type : PTHREAD_MUTEX_NORMAL, 0, 0};
  return 0;
}
int pthread_mutex_destroy(pthread_mutex_t* m) {
  return __atomic_load_n(&m->state, __ATOMIC_ACQUIRE) ? EBUSY : 0;
}

int pthread_mutex_lock(pthread_mutex_t* m) {
  if (m->type == PTHREAD_MUTEX_RECURSIVE &&
      __atomic_load_n(&m->owner, __ATOMIC_ACQUIRE) == reinterpret_cast<uintptr_t>(pthread_self())) {
    if (m->depth == UINT32_MAX) return EAGAIN;
    ++m->depth; return 0;
  }
  reinterpret_cast<Lock*>(m)->lock();
  if (m->type == PTHREAD_MUTEX_RECURSIVE) {
    m->depth = 1;
    __atomic_store_n(&m->owner, reinterpret_cast<uintptr_t>(pthread_self()), __ATOMIC_RELEASE);
  }
  return 0;
}

int pthread_mutex_trylock(pthread_mutex_t* m) {
  if (m->type == PTHREAD_MUTEX_RECURSIVE &&
      __atomic_load_n(&m->owner, __ATOMIC_ACQUIRE) == reinterpret_cast<uintptr_t>(pthread_self()))
    return pthread_mutex_lock(m);
  if (__sync_val_compare_and_swap(&m->state, 0u, 1u) != 0) return EBUSY;
  if (m->type == PTHREAD_MUTEX_RECURSIVE) {
    m->depth = 1;
    __atomic_store_n(&m->owner, reinterpret_cast<uintptr_t>(pthread_self()), __ATOMIC_RELEASE);
  }
  return 0;
}

int pthread_mutex_unlock(pthread_mutex_t* m) {
  if (m->type == PTHREAD_MUTEX_RECURSIVE) {
    if (__atomic_load_n(&m->owner, __ATOMIC_ACQUIRE) != reinterpret_cast<uintptr_t>(pthread_self())) return EPERM;
    if (--m->depth) return 0;
    __atomic_store_n(&m->owner, uintptr_t{0}, __ATOMIC_RELEASE);
  }
  reinterpret_cast<Lock*>(m)->unlock();
  return 0;
}

int pthread_cond_init(pthread_cond_t* c, const pthread_condattr_t*) {
  c->seq = 0;
  return 0;
}
int pthread_cond_destroy(pthread_cond_t*) { return 0; }

int pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m) {
  uint32_t seq = __atomic_load_n(&c->seq, __ATOMIC_ACQUIRE);
  pthread_mutex_unlock(m);
  (void)charm::futex_wait(&c->seq, seq, charm::kForever);
  pthread_mutex_lock(m);
  return 0;
}

int pthread_cond_timedwait(pthread_cond_t* c, pthread_mutex_t* m, const struct timespec* deadline) {
  if (!deadline || deadline->tv_sec < 0 || deadline->tv_nsec < 0 || deadline->tv_nsec >= 1000000000) return EINVAL;
  auto real = charm::realtime();
  if (!real.ok()) return errno_from(real.status);
  uint64_t seconds = static_cast<uint64_t>(deadline->tv_sec);
  if (seconds > (UINT64_MAX - static_cast<uint64_t>(deadline->tv_nsec)) / 1000000000) return EINVAL;
  uint64_t target = seconds * 1000000000 + static_cast<uint64_t>(deadline->tv_nsec);
  uint64_t now = charm::now_ns();
  uint64_t left = target > real.value ? target - real.value : 0;
  uint64_t until = left > UINT64_MAX - now ? UINT64_MAX : now + left;
  uint32_t seq = __atomic_load_n(&c->seq, __ATOMIC_ACQUIRE);
  int result = pthread_mutex_unlock(m);
  if (result) return result;
  auto status = charm::futex_wait(&c->seq, seq, until);
  result = pthread_mutex_lock(m);
  if (result) return result;
  return status == Status::TimedOut ? ETIMEDOUT : 0;
}

int pthread_cond_signal(pthread_cond_t* c) {
  __atomic_fetch_add(&c->seq, 1u, __ATOMIC_RELEASE);
  (void)charm::futex_wake(&c->seq, 1);
  return 0;
}

int pthread_cond_broadcast(pthread_cond_t* c) {
  __atomic_fetch_add(&c->seq, 1u, __ATOMIC_RELEASE);
  (void)charm::futex_wake(&c->seq, ~0ull);
  return 0;
}

int pthread_once(pthread_once_t* o, void (*fn)(void)) {
  // 0 — не вызывалась, 1 — идёт, 2 — готово.
  uint32_t expected = 0;
  if (__atomic_compare_exchange_n(&o->state, &expected, 1u, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
    fn();
    __atomic_store_n(&o->state, 2u, __ATOMIC_RELEASE);
    (void)charm::futex_wake(&o->state, ~0ull);
    return 0;
  }
  while (__atomic_load_n(&o->state, __ATOMIC_ACQUIRE) != 2) (void)charm::futex_wait(&o->state, 1, charm::kForever);
  return 0;
}

}  // extern "C"
