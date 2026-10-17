#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include "internal.hpp"
static_assert(sizeof(tm) == 36 && sizeof(timeval) == 16 && __builtin_offsetof(timeval, tv_usec) == 8);

namespace {
bool leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
}
extern "C" struct tm* gmtime_r(const time_t* value, struct tm* result) {
  if (!value || !result) { errno = EINVAL; return nullptr; }
  // Bound work and keep tm_year representable. UTC Gregorian years 1..9999.
  if (*value < -62135596800LL || *value > 253402300799LL) { errno = EOVERFLOW; return nullptr; }
  int64_t days = *value / 86400;
  int64_t seconds = *value % 86400;
  if (seconds < 0) { seconds += 86400; --days; }
  struct tm out{};
  out.tm_sec = static_cast<int>(seconds % 60);
  out.tm_min = static_cast<int>(seconds / 60 % 60);
  out.tm_hour = static_cast<int>(seconds / 3600);
  out.tm_wday = static_cast<int>((days % 7 + 11) % 7);
  int year = 1970;
  while (days < 0) { --year; days += leap(year) ? 366 : 365; }
  while (days >= (leap(year) ? 366 : 365)) { days -= leap(year) ? 366 : 365; ++year; }
  out.tm_year = year - 1900;
  out.tm_yday = static_cast<int>(days);
  constexpr int lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int month = 0;
  while (days >= lengths[month] + (month == 1 && leap(year) ? 1 : 0)) {
    days -= lengths[month] + (month == 1 && leap(year) ? 1 : 0); ++month;
  }
  out.tm_mon = month; out.tm_mday = static_cast<int>(days) + 1;
  *result = out;
  return result;
}
extern "C" struct tm* gmtime(const time_t* value) {
  return gmtime_r(value, &libc::current_thread()->utc_time);
}
extern "C" int gettimeofday(struct timeval* value, void* timezone) {
  if (!value) return libc::fail_errno(EFAULT);
  if (timezone) return libc::fail_errno(EINVAL);
  auto result = charm::realtime();
  if (!result.ok()) return libc::fail(result.status);
  value->tv_sec = static_cast<time_t>(result.value / 1000000000);
  value->tv_usec = static_cast<suseconds_t>(result.value % 1000000000 / 1000);
  return 0;
}
