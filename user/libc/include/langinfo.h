#ifndef _CHARMOS_LANGINFO_H
#define _CHARMOS_LANGINFO_H
#include <locale.h>
typedef int nl_item;
#define CODESET 1
#define RADIXCHAR 2
#define THOUSEP 3
#define D_T_FMT 4
#define D_FMT 5
#define T_FMT 6
#define T_FMT_AMPM 7
#define AM_STR 8
#define PM_STR 9
#define DAY_1 10
#define DAY_2 11
#define DAY_3 12
#define DAY_4 13
#define DAY_5 14
#define DAY_6 15
#define DAY_7 16
#define ABDAY_1 17
#define ABDAY_2 18
#define ABDAY_3 19
#define ABDAY_4 20
#define ABDAY_5 21
#define ABDAY_6 22
#define ABDAY_7 23
#define MON_1 24
#define MON_2 25
#define MON_3 26
#define MON_4 27
#define MON_5 28
#define MON_6 29
#define MON_7 30
#define MON_8 31
#define MON_9 32
#define MON_10 33
#define MON_11 34
#define MON_12 35
#define ABMON_1 36
#define ABMON_2 37
#define ABMON_3 38
#define ABMON_4 39
#define ABMON_5 40
#define ABMON_6 41
#define ABMON_7 42
#define ABMON_8 43
#define ABMON_9 44
#define ABMON_10 45
#define ABMON_11 46
#define ABMON_12 47
#ifdef __cplusplus
extern "C" {
#endif
char* nl_langinfo(nl_item);
char* nl_langinfo_l(nl_item, locale_t);
#ifdef __cplusplus
}
#endif
#endif
