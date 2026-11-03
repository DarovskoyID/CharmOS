#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <time.h>

namespace {
constexpr const char* months[] = {"January","February","March","April","May","June",
                                "July","August","September","October","November","December"};
constexpr const char* weekdays[] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
struct Parser {
  const char* input;
  tm value;
  int century = -1, short_year = -1, ampm = -1;
  bool have_year = false, have_month = false, have_day = false, hour12 = false;

  bool number(int& result, unsigned digits, int minimum, int maximum) {
    int parsed = 0;
    unsigned count = 0;
    while (count < digits && *input >= '0' && *input <= '9') {
      parsed = parsed * 10 + (*input++ - '0');
      ++count;
    }
    if (!count || parsed < minimum || parsed > maximum) return false;
    result = parsed;
    return true;
  }
  bool word(const char* word, size_t length) {
    for (size_t i=0;i<length;++i) {
      if (!input[i] || tolower(static_cast<unsigned char>(input[i])) !=
                       tolower(static_cast<unsigned char>(word[i]))) return false;
    }
    input += length;
    return true;
  }
  bool name(const char* const* names, unsigned count, bool abbreviated, int& result) {
    const char* start = input;
    for (unsigned i=0;i<count;++i) {
      input = start;
      if (word(names[i], abbreviated ? 3 : strlen(names[i]))) {
        result = static_cast<int>(i);
        return true;
      }
    }
    input = start;
    return false;
  }
  bool parse(const char* format) {
    for (;*format;++format) {
      if (isspace(static_cast<unsigned char>(*format))) {
        while (isspace(static_cast<unsigned char>(*input))) ++input;
        continue;
      }
      if (*format != '%') { if (*input++ != *format) return false; continue; }
      ++format;
      if (*format == 'E' || *format == 'O') ++format; // Fixed C locale.
      if (!*format) return false;
      int number_value = 0;
      switch (*format) {
        case '%': if (*input++ != '%') return false; break;
        case 'n': case 't': while (isspace(static_cast<unsigned char>(*input))) ++input; break;
        case 'Y':
          if (!number(number_value,4,1,9999)) return false;
          value.tm_year=number_value-1900; have_year=true;
          century=short_year=-1;
          break;
        case 'C': if (!number(century,2,0,99)) return false; have_year=true; break;
        case 'y': if (!number(short_year,2,0,99)) return false; have_year=true; break;
        case 'm':
          if (!number(number_value,2,1,12)) return false;
          value.tm_mon=number_value-1; have_month=true; break;
        case 'b': case 'h': case 'B':
          if (!name(months,12,*format!='B',value.tm_mon)) return false;
          have_month=true; break;
        case 'a': case 'A':
          if (!name(weekdays,7,*format=='a',value.tm_wday)) return false;
          break;
        case 'e': if (*input==' ') ++input; [[fallthrough]];
        case 'd': if (!number(value.tm_mday,2,1,31)) return false; have_day=true; break;
        case 'H': if (!number(value.tm_hour,2,0,23)) return false; hour12=false; break;
        case 'I': if (!number(value.tm_hour,2,1,12)) return false; hour12=true; break;
        case 'M': if (!number(value.tm_min,2,0,59)) return false; break;
        case 'S': if (!number(value.tm_sec,2,0,60)) return false; break;
        case 'j':
          if (!number(number_value,3,1,366)) return false;
          value.tm_yday=number_value-1; break;
        case 'w': if (!number(value.tm_wday,1,0,6)) return false; break;
        case 'u':
          if (!number(number_value,1,1,7)) return false;
          value.tm_wday=number_value%7; break;
        case 'p': {
          const char* start=input;
          if (word("AM",2)) ampm=0;
          else { input=start; if (!word("PM",2)) return false; ampm=1; }
          break;
        }
        case 'Z': {
          const char* start=input;
          if (!word("UTC",3)) { input=start; if (!word("GMT",3)) return false; }
          value.tm_isdst=0; break;
        }
        case 'z': {
          if (*input=='Z') { ++input; value.tm_isdst=0; break; }
          if (*input!='+' && *input!='-') return false;
          ++input;
          // The native tm has no offset field. Only UTC is representable.
          for (unsigned i=0;i<4;++i) if (*input++!='0') return false;
          value.tm_isdst=0; break;
        }
        case 'F': if (!parse("%Y-%m-%d")) return false; break;
        case 'D': case 'x': if (!parse("%m/%d/%y")) return false; break;
        case 'R': if (!parse("%H:%M")) return false; break;
        case 'T': case 'X': if (!parse("%H:%M:%S")) return false; break;
        case 'r': if (!parse("%I:%M:%S %p")) return false; break;
        case 'c': if (!parse("%a %b %e %H:%M:%S %Y")) return false; break;
        default: return false;
      }
    }
    return true;
  }
  bool finish() {
    if (short_year>=0 || century>=0) {
      int year=century>=0 ? century*100+(short_year>=0 ? short_year : 0)
                         : short_year+(short_year<=68 ? 2000 : 1900);
      if (year<1) return false;
      value.tm_year=year-1900;
    }
    if (hour12 && ampm>=0) value.tm_hour=value.tm_hour%12+(ampm==1 ? 12 : 0);
    if (have_year && have_month && have_day) {
      tm date{};
      date.tm_year=value.tm_year; date.tm_mon=value.tm_mon; date.tm_mday=value.tm_mday;
      int day=date.tm_mday;
      if (timegm(&date)==-1 || date.tm_mday!=day) return false;
      value.tm_wday=date.tm_wday;
      value.tm_yday=date.tm_yday;
    }
    return true;
  }
};
}

extern "C" char* strptime(const char* input, const char* format, tm* value) {
  if (!input || !format || !value) { errno=EINVAL; return nullptr; }
  Parser parser{input,*value};
  int saved_errno=errno;
  if (!parser.parse(format) || !parser.finish()) { errno=EINVAL; return nullptr; }
  *value=parser.value;
  errno=saved_errno;
  return const_cast<char*>(parser.input);
}
