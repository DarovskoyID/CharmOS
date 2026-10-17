#pragma once
#include <stdint.h>
namespace lib::core {
constexpr bool leap_year(unsigned year) { return year%4==0 && (year%100!=0 || year%400==0); }
// UEFI 2.11 §8.3: UTC = local time + timezone_to_utc_minutes.
inline bool utc_nanoseconds(unsigned year,unsigned month,unsigned day,unsigned hour,
                            unsigned minute,unsigned second,unsigned nanosecond,
                            int timezone_to_utc_minutes,uint64_t* out) {
  constexpr unsigned months[]={31,28,31,30,31,30,31,31,30,31,30,31};
  if(!out || year<1970 || year>2553 || month<1 || month>12 || day<1 ||
     hour>23 || minute>59 || second>59 || nanosecond>=1000000000 ||
     timezone_to_utc_minutes< -1440 || timezone_to_utc_minutes>1440) return false;
  unsigned maximum=months[month-1]+(month==2 && leap_year(year)?1u:0u);
  if(day>maximum) return false;
  uint64_t days=day-1;
  for(unsigned y=1970;y<year;++y) days+=leap_year(y)?366u:365u;
  for(unsigned m=1;m<month;++m) days+=months[m-1]+(m==2 && leap_year(year)?1u:0u);
  int64_t seconds=static_cast<int64_t>(days*86400+hour*3600+minute*60+second)+
                  static_cast<int64_t>(timezone_to_utc_minutes)*60;
  if(seconds<0 || static_cast<uint64_t>(seconds)>(UINT64_MAX-nanosecond)/1000000000) return false;
  *out=static_cast<uint64_t>(seconds)*1000000000+nanosecond; return true;
}
}
