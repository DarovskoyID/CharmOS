#ifndef _WCHAR_H
#define _WCHAR_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <bits/mbstate_t.h>
#ifndef __cplusplus
typedef __WCHAR_TYPE__ wchar_t;
#endif
typedef uint32_t wint_t;
#define WEOF ((wint_t)0xffffffffu)
#ifndef WCHAR_MIN
#define WCHAR_MIN (-__WCHAR_MAX__ - 1)
#endif
#ifndef WCHAR_MAX
#define WCHAR_MAX __WCHAR_MAX__
#endif
#ifdef __cplusplus
extern "C" {
#endif
size_t wcslen(const wchar_t* s);
size_t wcsnlen(const wchar_t* s, size_t n);
wchar_t* wcscpy(wchar_t* dst, const wchar_t* src);
wchar_t* wcsncpy(wchar_t* dst, const wchar_t* src, size_t n);
wchar_t* wcscat(wchar_t* dst, const wchar_t* src);
wchar_t* wcsncat(wchar_t* dst, const wchar_t* src, size_t n);
int wcscmp(const wchar_t* a, const wchar_t* b);
int wcsncmp(const wchar_t* a, const wchar_t* b, size_t n);
wchar_t* wcschr(const wchar_t* s, wchar_t c);
wchar_t* wcsrchr(const wchar_t* s, wchar_t c);
wchar_t* wcsstr(const wchar_t* s, const wchar_t* needle);
wchar_t* wcspbrk(const wchar_t* s, const wchar_t* accept);
size_t wcsspn(const wchar_t* s, const wchar_t* accept);
size_t wcscspn(const wchar_t* s, const wchar_t* reject);
wchar_t* wcstok(wchar_t* s, const wchar_t* delim, wchar_t** save);
wchar_t* wmemcpy(wchar_t* dst, const wchar_t* src, size_t n);
wchar_t* wmemmove(wchar_t* dst, const wchar_t* src, size_t n);
wchar_t* wmemset(wchar_t* dst, wchar_t c, size_t n);
int wmemcmp(const wchar_t* a, const wchar_t* b, size_t n);
wchar_t* wmemchr(const wchar_t* s, wchar_t c, size_t n);
int mbsinit(const mbstate_t* state);
size_t mbrtowc(wchar_t* out, const char* s, size_t n, mbstate_t* state);
size_t mbrlen(const char* s, size_t n, mbstate_t* state);
size_t wcrtomb(char* out, wchar_t c, mbstate_t* state);
size_t mbsrtowcs(wchar_t* out, const char** s, size_t n, mbstate_t* state);
size_t wcsrtombs(char* out, const wchar_t** s, size_t n, mbstate_t* state);
wint_t btowc(int c);
int wctob(wint_t c);
int wcscoll(const wchar_t* a, const wchar_t* b);
size_t wcsxfrm(wchar_t* out, const wchar_t* s, size_t n);
#ifdef __cplusplus
}
#endif
#endif
