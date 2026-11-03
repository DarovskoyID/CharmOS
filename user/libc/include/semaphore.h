#pragma once
#include <stdint.h>
#include <limits.h>
#include <time.h>

#define SEM_VALUE_MAX INT_MAX
typedef struct {
  uint32_t __count, __waiters, __magic, __reserved;
} sem_t;

#ifdef __cplusplus
extern "C" {
#endif
int sem_init(sem_t* semaphore,int pshared,unsigned value);
int sem_destroy(sem_t* semaphore);
int sem_wait(sem_t* semaphore);
int sem_trywait(sem_t* semaphore);
int sem_timedwait(sem_t* semaphore,const struct timespec* deadline);
int sem_post(sem_t* semaphore);
int sem_getvalue(sem_t* semaphore,int* value);
#ifdef __cplusplus
}
#endif
