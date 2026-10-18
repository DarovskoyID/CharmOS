#include <errno.h>
#include <langinfo.h>
#include <stdint.h>
#include <time.h>

namespace {
bool leap(int y) { return y%4==0 && (y%100!=0 || y%400==0); }
struct Output {
  char* out; size_t cap, count=0; bool overflow=false;
  void put(char c) {
    if(count+1<cap) out[count]=c; else overflow=true;
    ++count;
  }
  void text(const char* s) { while(*s) put(*s++); }
  void number(unsigned value,unsigned width,char fill='0') {
    char digits[16]; unsigned n=0;
    do { digits[n++]=static_cast<char>('0'+value%10); value/=10; } while(value);
    for(unsigned i=n;i<width;++i) put(fill);
    while(n) put(digits[--n]);
  }
};
bool in_range(int n,int first,int last) { return n>=first && n<=last; }
bool format(Output& out,const char* pattern,const tm& t,unsigned depth=0) {
  if(depth>8) return false;
  for(;*pattern;++pattern) {
    if(*pattern!='%') { out.put(*pattern); continue; }
    ++pattern;
    if(*pattern=='E' || *pattern=='O') ++pattern; // No alternative era/digits in this locale.
    if(!*pattern) return false;
    int64_t full_year=static_cast<int64_t>(t.tm_year)+1900;
    int year=full_year>=1 && full_year<=9999 ? static_cast<int>(full_year) : 0;
    switch(*pattern) {
      case '%': out.put('%'); break;
      case 'n': out.put('\n'); break;
      case 't': out.put('\t'); break;
      case 'a': case 'A':
        if(!in_range(t.tm_wday,0,6)) return false;
        out.text(nl_langinfo((*pattern=='a'?ABDAY_1:DAY_1)+t.tm_wday)); break;
      case 'b': case 'h': case 'B':
        if(!in_range(t.tm_mon,0,11)) return false;
        out.text(nl_langinfo((*pattern=='B'?MON_1:ABMON_1)+t.tm_mon)); break;
      case 'c': if(!format(out,nl_langinfo(D_T_FMT),t,depth+1)) return false; break;
      case 'x': if(!format(out,nl_langinfo(D_FMT),t,depth+1)) return false; break;
      case 'X': if(!format(out,nl_langinfo(T_FMT),t,depth+1)) return false; break;
      case 'D': if(!format(out,"%m/%d/%y",t,depth+1)) return false; break;
      case 'F': if(!format(out,"%Y-%m-%d",t,depth+1)) return false; break;
      case 'R': if(!format(out,"%H:%M",t,depth+1)) return false; break;
      case 'T': if(!format(out,"%H:%M:%S",t,depth+1)) return false; break;
      case 'r': if(!format(out,nl_langinfo(T_FMT_AMPM),t,depth+1)) return false; break;
      case 'C': if(!year) return false; out.number(static_cast<unsigned>(year/100),2); break;
      case 'Y': if(!year) return false; out.number(static_cast<unsigned>(year),4); break;
      case 'y': if(!year) return false; out.number(static_cast<unsigned>(year%100),2); break;
      case 'd': case 'e':
        if(!in_range(t.tm_mday,1,31)) return false;
        out.number(static_cast<unsigned>(t.tm_mday),2,*pattern=='e'?' ':'0'); break;
      case 'm':
        if(!in_range(t.tm_mon,0,11)) return false;
        out.number(static_cast<unsigned>(t.tm_mon+1),2); break;
      case 'H': case 'I': case 'k': case 'l': {
        if(!in_range(t.tm_hour,0,23)) return false;
        int h=(*pattern=='I' || *pattern=='l')?(t.tm_hour%12 ? t.tm_hour%12 : 12):t.tm_hour;
        out.number(static_cast<unsigned>(h),2,(*pattern=='k' || *pattern=='l')?' ':'0'); break;
      }
      case 'M':
        if(!in_range(t.tm_min,0,59)) return false;
        out.number(static_cast<unsigned>(t.tm_min),2); break;
      case 'S':
        if(!in_range(t.tm_sec,0,60)) return false;
        out.number(static_cast<unsigned>(t.tm_sec),2); break;
      case 'j':
        if(!in_range(t.tm_yday,0,365)) return false;
        out.number(static_cast<unsigned>(t.tm_yday+1),3); break;
      case 'p': case 'P':
        if(!in_range(t.tm_hour,0,23)) return false;
        out.text(*pattern=='P'?(t.tm_hour<12?"am":"pm"):nl_langinfo(t.tm_hour<12?AM_STR:PM_STR)); break;
      case 'w': case 'u':
        if(!in_range(t.tm_wday,0,6)) return false;
        out.number(static_cast<unsigned>(*pattern=='u'?(t.tm_wday?t.tm_wday:7):t.tm_wday),1); break;
      case 'U': case 'W':
        if(!in_range(t.tm_wday,0,6) || !in_range(t.tm_yday,0,365)) return false;
        out.number(static_cast<unsigned>((t.tm_yday+7-(*pattern=='W'?(t.tm_wday+6)%7:t.tm_wday))/7),2); break;
      case 'V': case 'G': case 'g': {
        if(!year || !in_range(t.tm_wday,0,6) || !in_range(t.tm_yday,0,365)) return false;
        int weekday=t.tm_wday ? t.tm_wday : 7;
        int week=(t.tm_yday+1-weekday+10)/7;
        int jan1=(t.tm_wday-t.tm_yday%7+7)%7;
        int weeks=(jan1==4 || (jan1==3 && leap(year))) ? 53 : 52;
        if(week<1) {
          --year; jan1=(jan1-(365+(leap(year)?1:0))%7+7)%7;
          week=(jan1==4 || (jan1==3 && leap(year))) ? 53 : 52;
        } else if(week>weeks) { ++year; week=1; }
        out.number(static_cast<unsigned>(*pattern=='V'?week:(*pattern=='g'?year%100:year)),*pattern=='G'?4:2); break;
      }
      case 'z': out.text("+0000"); break;
      case 'Z': out.text("UTC"); break;
      default: return false;
    }
  }
  return true;
}
}

extern "C" size_t strftime(char* destination,size_t capacity,const char* pattern,const tm* value) {
  if(!pattern || !value || (capacity && !destination)) { errno=EINVAL; return 0; }
  Output out{destination,capacity};
  bool valid=format(out,pattern,*value);
  if(capacity) destination[out.count<capacity ? out.count : capacity-1]=0;
  if(!valid) { errno=EINVAL; return 0; }
  return out.overflow ? 0 : out.count;
}
