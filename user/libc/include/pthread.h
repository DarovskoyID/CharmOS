#ifndef _PTHREAD_H
#define _PTHREAD_H
#include <stdint.h>
#include <time.h>
typedef struct __pthread* pthread_t;
typedef struct { int unused; } pthread_attr_t;
typedef struct { volatile uint32_t state; } pthread_mutex_t;
typedef struct { int unused; } pthread_mutexattr_t;
typedef struct { volatile uint32_t seq; } pthread_cond_t;
typedef struct { int unused; } pthread_condattr_t;
typedef struct { volatile uint32_t state; } pthread_once_t;
typedef struct {
  pthread_mutex_t gate;
  pthread_cond_t changed;
  uint32_t readers;
  uint32_t writer;
} pthread_rwlock_t;
typedef struct { int unused; } pthread_rwlockattr_t;
typedef uint32_t pthread_key_t;
#define PTHREAD_KEYS_MAX 128
#define PTHREAD_DESTRUCTOR_ITERATIONS 4
#define PTHREAD_RWLOCK_INITIALIZER {{0}, {0}, 0, 0}
#define PTHREAD_MUTEX_INITIALIZER {0}
#define PTHREAD_COND_INITIALIZER {0}
#define PTHREAD_ONCE_INIT {0}
#ifdef __cplusplus
extern "C" {
#endif
int pthread_create(pthread_t* t, const pthread_attr_t* attr, void* (*fn)(void*), void* arg);
int pthread_join(pthread_t t, void** ret);
int pthread_detach(pthread_t t);
pthread_t pthread_self(void);
__attribute__((noreturn)) void pthread_exit(void* ret);
int pthread_mutex_init(pthread_mutex_t* m, const pthread_mutexattr_t* a);
int pthread_mutex_destroy(pthread_mutex_t* m);
int pthread_mutex_lock(pthread_mutex_t* m);
int pthread_mutex_trylock(pthread_mutex_t* m);
int pthread_mutex_unlock(pthread_mutex_t* m);
int pthread_cond_init(pthread_cond_t* c, const pthread_condattr_t* a);
int pthread_cond_destroy(pthread_cond_t* c);
int pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m);
int pthread_cond_signal(pthread_cond_t* c);
int pthread_cond_broadcast(pthread_cond_t* c);
int pthread_once(pthread_once_t* o, void (*fn)(void));
int pthread_rwlock_init(pthread_rwlock_t* lock, const pthread_rwlockattr_t* attr);
int pthread_rwlock_destroy(pthread_rwlock_t* lock);
int pthread_rwlock_rdlock(pthread_rwlock_t* lock);
int pthread_rwlock_wrlock(pthread_rwlock_t* lock);
int pthread_rwlock_tryrdlock(pthread_rwlock_t* lock);
int pthread_rwlock_trywrlock(pthread_rwlock_t* lock);
int pthread_rwlock_unlock(pthread_rwlock_t* lock);
int pthread_key_create(pthread_key_t* key, void (*destructor)(void*));
int pthread_key_delete(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void* value);
void* pthread_getspecific(pthread_key_t key);
#ifdef __cplusplus
}
#endif
#endif
