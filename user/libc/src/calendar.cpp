#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
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
extern "C" {
static char utc_zone[] = "UTC";
char* tzname[2] = {utc_zone, utc_zone};
long timezone = 0;
int daylight = 0;
void tzset(void) {
  const char* zone = getenv("TZ");
  if (zone && *zone && strcmp(zone, "UTC") && strcmp(zone, "UTC0") && strcmp(zone, "GMT0"))
    errno = EINVAL;
}
struct tm* localtime_r(const time_t* value, struct tm* result) {
  return gmtime_r(value, result);
}
struct tm* localtime(const time_t* value) { return gmtime(value); }
double difftime(time_t end, time_t begin) {
  // Subtract in unsigned magnitude first: neither signed time_t subtraction
  // nor rounding each operand separately is safe for large adjacent values.
  if (end >= begin) return static_cast<double>(static_cast<uint64_t>(end) - static_cast<uint64_t>(begin));
  return -static_cast<double>(static_cast<uint64_t>(begin) - static_cast<uint64_t>(end));
}
time_t mktime(struct tm* value) {
  if (!value) { errno = EINVAL; return -1; }
  if (value->tm_isdst > 0) { errno = EINVAL; return -1; }
  return timegm(value);
}

time_t timegm(struct tm* value) {
  if (!value) { errno = EINVAL; return -1; }
  int64_t year = static_cast<int64_t>(value->tm_year) + 1900;
  int64_t months = value->tm_mon;
  int64_t years = months / 12;
  int64_t month = months % 12;
  if (month < 0) { month += 12; --years; }
  year += years;
  if (year < 1 || year > 9999) { errno = EOVERFLOW; return -1; }
  auto leap_count = [](int64_t y) { return y / 4 - y / 100 + y / 400; };
  int64_t days = 365 * (year - 1970) + leap_count(year - 1) - leap_count(1969);
  constexpr int lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  for (int i = 0; i < month; ++i)
    days += lengths[i] + (i == 1 && leap(static_cast<int>(year)) ? 1 : 0);
  days += static_cast<int64_t>(value->tm_mday) - 1;
  time_t result = days * 86400 + static_cast<int64_t>(value->tm_hour) * 3600 +
                 static_cast<int64_t>(value->tm_min) * 60 + value->tm_sec;
  struct tm normalized;
  if (!gmtime_r(&result, &normalized)) return -1;
  *value = normalized;
  return result;
}
}
extern "C" int gettimeofday(struct timeval* value, void* legacy_zone) {
  if (!value) return libc::fail_errno(EFAULT);
  if (legacy_zone) return libc::fail_errno(EINVAL);
  auto result = charm::realtime();
  if (!result.ok()) return libc::fail(result.status);
  value->tv_sec = static_cast<time_t>(result.value / 1000000000);
  value->tv_usec = static_cast<suseconds_t>(result.value % 1000000000 / 1000);
  return 0;
}
