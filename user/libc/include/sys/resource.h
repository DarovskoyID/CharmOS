#ifndef _SYS_RESOURCE_H
#define _SYS_RESOURCE_H
#include <stdint.h>
typedef uint64_t rlim_t;
struct rlimit { rlim_t rlim_cur, rlim_max; };
#define RLIM_INFINITY ((rlim_t)-1)
#define RLIMIT_NOFILE 0
#define RLIMIT_STACK 1
#define RLIMIT_CORE 2
#ifdef __cplusplus
extern "C" {
#endif
int getrlimit(int resource, struct rlimit* limit);
int setrlimit(int resource, const struct rlimit* limit);
#ifdef __cplusplus
}
#endif
#endif
