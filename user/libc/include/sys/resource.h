#ifndef _SYS_RESOURCE_H
#define _SYS_RESOURCE_H
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
typedef uint64_t rlim_t;
struct rlimit { rlim_t rlim_cur, rlim_max; };
#define RLIM_INFINITY ((rlim_t)-1)
#define RLIMIT_NOFILE 0
#define RLIMIT_STACK 1
#define RLIMIT_CORE 2
#define RLIMIT_DATA 3
/* Source-compatible declaration. getrusage returns ENOTSUP until peak RSS
   and user/system accounting are available; output is unchanged on failure. */
#define RUSAGE_SELF 0
#define RUSAGE_CHILDREN -1
struct rusage {
  struct timeval ru_utime, ru_stime;
  long ru_maxrss, ru_ixrss, ru_idrss, ru_isrss, ru_minflt, ru_majflt,
       ru_nswap, ru_inblock, ru_oublock, ru_msgsnd, ru_msgrcv, ru_nsignals,
       ru_nvcsw, ru_nivcsw;
};
#ifdef __cplusplus
extern "C" {
#endif
int getrlimit(int resource, struct rlimit* limit);
int setrlimit(int resource, const struct rlimit* limit);
int getrusage(int who, struct rusage* usage);
#ifdef __cplusplus
}
#endif
#endif
