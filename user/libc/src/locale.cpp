#include <ctype.h>
#include <errno.h>
#include <langinfo.h>
#include <limits.h>
#include <locale.h>
#include <string.h>
#include <wctype.h>
#include "internal.hpp"

struct __charm_locale { unsigned marker; };
namespace {
constinit __charm_locale fixed_locale{0x554638};
bool valid(locale_t value) { return value == &fixed_locale; }
bool name_supported(const char* name) {
  return name && (!*name || !strcmp(name,"C") || !strcmp(name,"POSIX") ||
                 !strcmp(name,"C.UTF-8") || !strcmp(name,"C.utf8"));
}
char* text(const char* value) { return const_cast<char*>(value); }
const char* const days[] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
const char* const short_days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
const char* const months[] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
const char* const short_months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
constinit lconv conventions = {
  const_cast<char*>("."), const_cast<char*>(""), const_cast<char*>(""), const_cast<char*>(""), const_cast<char*>(""),
  const_cast<char*>(""), const_cast<char*>(""), const_cast<char*>(""), const_cast<char*>(""), const_cast<char*>(""),
  CHAR_MAX,CHAR_MAX,CHAR_MAX,CHAR_MAX,CHAR_MAX,CHAR_MAX,CHAR_MAX,
  CHAR_MAX,CHAR_MAX,CHAR_MAX,CHAR_MAX,CHAR_MAX,CHAR_MAX,CHAR_MAX
};
static_assert(sizeof(lconv) == 96);
}

extern "C" {
char* setlocale(int category, const char* name) {
  if (category < LC_CTYPE || category > LC_ALL) { errno=EINVAL; return nullptr; }
  if (name && !name_supported(name)) { errno=ENOENT; return nullptr; }
  return text("C.UTF-8");
}
lconv* localeconv(void) { return &conventions; }
locale_t newlocale(int mask, const char* name, locale_t base) {
  if ((mask & ~LC_ALL_MASK) || (base && !valid(base)) || !name) { errno=EINVAL; return nullptr; }
  if (!name_supported(name)) { errno=ENOENT; return nullptr; }
  return &fixed_locale;
}
locale_t duplocale(locale_t value) {
  if (!valid(value) && value != LC_GLOBAL_LOCALE) { errno=EINVAL; return nullptr; }
  return &fixed_locale;
}
void freelocale(locale_t value) { if (!valid(value)) errno=EINVAL; }
locale_t uselocale(locale_t value) {
  auto* thread=libc::current_thread();
  locale_t previous=thread->locale_selection ? static_cast<locale_t>(thread->locale_selection) : LC_GLOBAL_LOCALE;
  if (!value) return previous;
  if (!valid(value) && value != LC_GLOBAL_LOCALE) { errno=EINVAL; return nullptr; }
  thread->locale_selection=value==LC_GLOBAL_LOCALE ? nullptr : value;
  return previous;
}
char* nl_langinfo_l(nl_item item, locale_t value) {
  if (!valid(value) && value!=LC_GLOBAL_LOCALE) { errno=EINVAL; return text(""); }
  if (item>=DAY_1 && item<=DAY_7) return text(days[item-DAY_1]);
  if (item>=ABDAY_1 && item<=ABDAY_7) return text(short_days[item-ABDAY_1]);
  if (item>=MON_1 && item<=MON_12) return text(months[item-MON_1]);
  if (item>=ABMON_1 && item<=ABMON_12) return text(short_months[item-ABMON_1]);
  switch (item) {
    case CODESET: return text("UTF-8");
    case RADIXCHAR: return text(".");
    case THOUSEP: return text("");
    case D_T_FMT: return text("%a %b %e %H:%M:%S %Y");
    case D_FMT: return text("%m/%d/%y");
    case T_FMT: return text("%H:%M:%S");
    case T_FMT_AMPM: return text("%I:%M:%S %p");
    case AM_STR: return text("AM");
    case PM_STR: return text("PM");
    default: return text("");
  }
}
char* nl_langinfo(nl_item item) { return nl_langinfo_l(item,LC_GLOBAL_LOCALE); }
int strcoll(const char* a, const char* b) { return strcmp(a,b); }
size_t strxfrm(char* out, const char* src, size_t n) {
  size_t length=strlen(src);
  if (n) memcpy(out,src,length<n ? length+1 : n);
  return length;
}
int iscntrl(int c) { return (c>=0 && c<32) || c==127; }
int isgraph(int c) { return c>32 && c<127; }
int isblank(int c) { return c==' ' || c=='\t'; }
#define WIDE_CLASS(name) int isw##name(wint_t c) { return c<128 && is##name(static_cast<int>(c)); }
WIDE_CLASS(alnum) WIDE_CLASS(alpha) WIDE_CLASS(blank) WIDE_CLASS(cntrl)
WIDE_CLASS(digit) WIDE_CLASS(graph) WIDE_CLASS(lower) WIDE_CLASS(print)
WIDE_CLASS(punct) WIDE_CLASS(space) WIDE_CLASS(upper) WIDE_CLASS(xdigit)
#undef WIDE_CLASS
wint_t towlower(wint_t c) { return c<128 ? static_cast<wint_t>(tolower(static_cast<int>(c))) : c; }
wint_t towupper(wint_t c) { return c<128 ? static_cast<wint_t>(toupper(static_cast<int>(c))) : c; }
wctype_t wctype(const char* name) {
  static const char* const names[]={"alnum","alpha","blank","cntrl","digit","graph","lower","print","punct","space","upper","xdigit"};
  for (unsigned i=0;i<12;++i) if (!strcmp(name,names[i])) return i+1;
  return 0;
}
int iswctype(wint_t c,wctype_t type) {
  switch(type) {
    case 1:return iswalnum(c); case 2:return iswalpha(c); case 3:return iswblank(c);
    case 4:return iswcntrl(c); case 5:return iswdigit(c); case 6:return iswgraph(c);
    case 7:return iswlower(c); case 8:return iswprint(c); case 9:return iswpunct(c);
    case 10:return iswspace(c); case 11:return iswupper(c); case 12:return iswxdigit(c);
    default:return 0;
  }
}
wctrans_t wctrans(const char* name) { return !strcmp(name,"tolower") ? 1 : (!strcmp(name,"toupper") ? 2 : 0); }
wint_t towctrans(wint_t c,wctrans_t type) { return type==1 ? towlower(c) : (type==2 ? towupper(c) : c); }
}
