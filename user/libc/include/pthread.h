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
#ifdef __cplusplus
}
#endif
#endif
