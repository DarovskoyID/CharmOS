#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <charm/sys.hpp>

#define CHECK(c) do { if (!(c)) { printf("threads runtime: FAILED line %d\n", __LINE__); exit(1); } } while (0)
namespace {
pthread_key_t g_key, g_repeat;
unsigned g_destroyed = 0, g_repeated = 0;
pthread_rwlock_t g_lock = PTHREAD_RWLOCK_INITIALIZER;
unsigned g_x = 0, g_y = 0;
unsigned g_readers_ready = 0;
void destroy_value(void* value) {
  CHECK(value != nullptr && pthread_getspecific(g_key) == nullptr);
  __atomic_fetch_add(&g_destroyed, 1u, __ATOMIC_RELAXED);
}
void repeat_value(void* value) {
  CHECK(pthread_getspecific(g_repeat) == nullptr);
  __atomic_fetch_add(&g_repeated, 1u, __ATOMIC_RELAXED);
  CHECK(pthread_setspecific(g_repeat, value) == 0);
}
void* specific_worker(void* token) {
  CHECK(pthread_getspecific(g_key) == nullptr);
  CHECK(pthread_setspecific(g_key, token) == 0);
  CHECK(pthread_setspecific(g_repeat, token) == 0);
  charm::yield();
  CHECK(pthread_getspecific(g_key) == token);
  if (token == reinterpret_cast<void*>(2)) pthread_exit(token);
  return token;
}
void* writer(void*) {
  for (unsigned i = 0; i < 500; ++i) {
    CHECK(pthread_rwlock_wrlock(&g_lock) == 0);
    ++g_x;
    charm::yield();
    g_y = g_x * 2;
    CHECK(pthread_rwlock_unlock(&g_lock) == 0);
  }
  return nullptr;
}
void* reader(void*) {
  CHECK(pthread_rwlock_rdlock(&g_lock) == 0);
  __atomic_fetch_add(&g_readers_ready, 1u, __ATOMIC_RELEASE);
  while (__atomic_load_n(&g_readers_ready, __ATOMIC_ACQUIRE) != 2) charm::yield();
  CHECK(pthread_rwlock_unlock(&g_lock) == 0);
  for (unsigned i = 0; i < 1000; ++i) {
    CHECK(pthread_rwlock_rdlock(&g_lock) == 0);
    CHECK(pthread_rwlock_rdlock(&g_lock) == 0);
    CHECK(g_y == g_x * 2);
    CHECK(pthread_rwlock_trywrlock(&g_lock) == EBUSY);
    CHECK(pthread_rwlock_unlock(&g_lock) == 0);
    CHECK(pthread_rwlock_unlock(&g_lock) == 0);
  }
  return nullptr;
}
}
extern "C" int main() {
  CHECK(pthread_key_create(&g_key, destroy_value) == 0);
  CHECK(pthread_key_create(&g_repeat, repeat_value) == 0);
  CHECK(pthread_setspecific(g_key, reinterpret_cast<void*>(99)) == 0);
  pthread_t a, b;
  CHECK(pthread_create(&a, nullptr, specific_worker, reinterpret_cast<void*>(1)) == 0);
  CHECK(pthread_create(&b, nullptr, specific_worker, reinterpret_cast<void*>(2)) == 0);
  void* ret;
  CHECK(pthread_join(a, &ret) == 0 && ret == reinterpret_cast<void*>(1));
  CHECK(pthread_join(b, &ret) == 0 && ret == reinterpret_cast<void*>(2));
  CHECK(g_destroyed == 2 && g_repeated == 8);
  CHECK(pthread_getspecific(g_key) == reinterpret_cast<void*>(99));
  auto old = g_key;
  CHECK(pthread_key_delete(g_key) == 0 && g_destroyed == 2);
  CHECK(pthread_key_create(&g_key, nullptr) == 0);
  CHECK(g_key != old && pthread_getspecific(g_key) == nullptr);
  CHECK(pthread_setspecific(old, nullptr) == EINVAL);
  CHECK(pthread_key_delete(g_key) == 0 && pthread_key_delete(g_repeat) == 0);
  pthread_key_t keys[PTHREAD_KEYS_MAX], extra;
  errno = 73;
  for (auto& key : keys) CHECK(pthread_key_create(&key, nullptr) == 0);
  CHECK(pthread_key_create(&extra, nullptr) == EAGAIN);
  CHECK(errno == 73);
  for (auto key : keys) CHECK(pthread_key_delete(key) == 0);
  printf("threads runtime: isolated keys, reuse and destructors ok\n");
  CHECK(pthread_rwlock_rdlock(&g_lock) == 0);
  CHECK(pthread_rwlock_tryrdlock(&g_lock) == 0);
  CHECK(pthread_rwlock_destroy(&g_lock) == EBUSY);
  CHECK(pthread_rwlock_trywrlock(&g_lock) == EBUSY);
  CHECK(pthread_rwlock_unlock(&g_lock) == 0 && pthread_rwlock_unlock(&g_lock) == 0);
  CHECK(pthread_rwlock_trywrlock(&g_lock) == 0);
  CHECK(pthread_rwlock_tryrdlock(&g_lock) == EBUSY);
  CHECK(pthread_rwlock_unlock(&g_lock) == 0);
  pthread_t threads[6];
  for (unsigned i = 0; i < 6; ++i)
    CHECK(pthread_create(&threads[i], nullptr, i < 4 ? writer : reader, nullptr) == 0);
  for (auto thread : threads) CHECK(pthread_join(thread, nullptr) == 0);
  CHECK(g_x == 2000 && g_y == 4000);
  CHECK(pthread_rwlock_destroy(&g_lock) == 0);
  printf("threads runtime: concurrent readers and writers ok\n");
  return 0;
}
