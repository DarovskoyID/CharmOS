#ifndef _TIME_H
#define _TIME_H
#include <sys/types.h>
#include <stddef.h>
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID 3
#define CLOCK_MONOTONIC_RAW 4
#define CLOCK_BOOTTIME 7
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
struct tm* localtime_r(const time_t* value, struct tm* result);
struct tm* localtime(const time_t* value);
time_t mktime(struct tm* value);
double difftime(time_t end, time_t begin);
void tzset(void);
extern char* tzname[2];
extern long timezone;
extern int daylight;
size_t strftime(char*, size_t, const char*, const struct tm*);
#ifdef __cplusplus
}
#endif
#endif
