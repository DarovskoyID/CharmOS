#ifndef _SYS_TIME_H
#define _SYS_TIME_H
#include <sys/types.h>
typedef long suseconds_t;
struct timeval { time_t tv_sec; suseconds_t tv_usec; };
#ifdef __cplusplus
extern "C" {
#endif
int gettimeofday(struct timeval* value, void* timezone);
#ifdef __cplusplus
}
#endif
#endif
