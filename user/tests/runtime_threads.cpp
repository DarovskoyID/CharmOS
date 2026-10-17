#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <charm/sys.hpp>

#define CHECK(c) do { if (!(c)) { printf("threads runtime: FAILED line %d\n", __LINE__); exit(1); } } while (0)
#include "unicode_checks.hpp"
#include "cxx_checks.hpp"
namespace {
pthread_key_t g_key, g_repeat;
unsigned g_destroyed = 0, g_repeated = 0;
pthread_rwlock_t g_lock = PTHREAD_RWLOCK_INITIALIZER;
unsigned g_x = 0, g_y = 0;
unsigned g_readers_ready = 0;
pthread_mutex_t g_wait_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_wait_cond = PTHREAD_COND_INITIALIZER;
bool g_wait_ready = false;
void* wake_waiter(void*) {
  CHECK(pthread_mutex_lock(&g_wait_mutex) == 0);
  g_wait_ready = true;
  CHECK(pthread_cond_signal(&g_wait_cond) == 0);
  CHECK(pthread_mutex_unlock(&g_wait_mutex) == 0);
  return nullptr;
}
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
  CHECK(cxx_local().value == 77);
  wchar_t scalar;
  CHECK(mbrtowc(&scalar, "\xd0", 1, nullptr) == static_cast<size_t>(-2));
  charm::yield();
  CHECK(mbrtowc(&scalar, "\x90", 1, nullptr) == 1 && scalar == L'А');
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
  cxx_prepare();
  unicode_checks();
  struct CalendarCase { time_t seconds; int year, month, day, weekday, yearday; };
  const CalendarCase dates[] = {{0,1970,1,1,4,0}, {-1,1969,12,31,3,364},
    {951782400,2000,2,29,2,59}, {4107542400LL,2100,3,1,1,59},
    {-62135596800LL,1,1,1,1,0}, {253402300799LL,9999,12,31,5,364}};
  for (auto date : dates) {
    tm result{};
    CHECK(gmtime_r(&date.seconds, &result) == &result);
    CHECK(result.tm_year == date.year - 1900 && result.tm_mon == date.month - 1 &&
      result.tm_mday == date.day && result.tm_wday == date.weekday && result.tm_yday == date.yearday);
    CHECK(result.tm_isdst == 0);
  }
  time_t beyond = 253402300800LL;
  CHECK(gmtime(&beyond) == nullptr && errno == EOVERFLOW);
  int sorted[] = {1,3,5,7};
  auto compare = [](const void* a, const void* b) { return *static_cast<const int*>(a) - *static_cast<const int*>(b); };
  int search_key = 5;
  CHECK(bsearch(&search_key, sorted, 4, sizeof(int), compare) == sorted + 2);
  search_key = 4; CHECK(bsearch(&search_key, sorted, 4, sizeof(int), compare) == nullptr);
  pthread_mutexattr_t attr;
  CHECK(pthread_mutexattr_init(&attr) == 0);
  CHECK(pthread_mutexattr_settype(&attr, 99) == EINVAL);
  CHECK(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) == 0);
  pthread_mutex_t recursive;
  CHECK(pthread_mutex_init(&recursive, &attr) == 0);
  CHECK(pthread_mutex_lock(&recursive) == 0 && pthread_mutex_trylock(&recursive) == 0);
  CHECK(pthread_mutex_unlock(&recursive) == 0 && pthread_mutex_unlock(&recursive) == 0);
  CHECK(pthread_mutex_unlock(&recursive) == EPERM);
  CHECK(pthread_mutex_destroy(&recursive) == 0 && pthread_mutexattr_destroy(&attr) == 0);
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
  CHECK(pthread_mutex_lock(&mutex) == 0);
  timespec deadline{};
  CHECK(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
  CHECK(pthread_cond_timedwait(&cond, &mutex, &deadline) == ETIMEDOUT);
  CHECK(pthread_mutex_trylock(&mutex) == EBUSY);
  CHECK(pthread_mutex_unlock(&mutex) == 0);
  CHECK(pthread_mutex_lock(&g_wait_mutex) == 0);
  pthread_t notifier;
  CHECK(pthread_create(&notifier, nullptr, wake_waiter, nullptr) == 0);
  CHECK(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
  deadline.tv_sec += 10;
  while (!g_wait_ready) CHECK(pthread_cond_timedwait(&g_wait_cond, &g_wait_mutex, &deadline) == 0);
  CHECK(pthread_mutex_unlock(&g_wait_mutex) == 0 && pthread_join(notifier, nullptr) == 0);
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
