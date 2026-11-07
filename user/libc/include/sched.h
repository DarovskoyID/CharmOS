#ifndef _SCHED_H
#define _SCHED_H
#include <pthread.h>
#include <sys/types.h>
#define SCHED_OTHER 0
#define SCHED_FIFO 1
#define SCHED_RR 2
struct sched_param { int sched_priority; };
#ifdef __cplusplus
extern "C" {
#endif
int sched_yield(void);
int sched_get_priority_min(int policy);
int sched_get_priority_max(int policy);
int sched_setscheduler(pid_t pid, int policy, const struct sched_param* param);
int pthread_setschedparam(pthread_t t, int policy, const struct sched_param* param);
#ifdef __cplusplus
}
#endif
#endif
