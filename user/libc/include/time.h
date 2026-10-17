#ifndef _TIME_H
#define _TIME_H
#include <sys/types.h>
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
typedef int clockid_t;
struct timespec {
  time_t tv_sec;
  long tv_nsec;
};
struct tm {
  int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
};
#ifdef __cplusplus
extern "C" {
#endif
int clock_gettime(clockid_t clk, struct timespec* ts);
int nanosleep(const struct timespec* req, struct timespec* rem);
time_t time(time_t* t);
struct tm* gmtime_r(const time_t* value, struct tm* result);
struct tm* gmtime(const time_t* value);
#ifdef __cplusplus
}
#endif
#endif
