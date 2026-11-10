#ifndef _STDLIB_H
#define _STDLIB_H
#include <stddef.h>
#include <alloca.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 0x7fffffff
#define MB_CUR_MAX 4
typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
typedef struct { long long quot, rem; } lldiv_t;
#ifdef __cplusplus
extern "C" {
#endif
void* malloc(size_t size);
void* aligned_alloc(size_t alignment, size_t size);
int posix_memalign(void** memptr, size_t alignment, size_t size);
void* calloc(size_t n, size_t size);
void* realloc(void* p, size_t size);
void free(void* p);
__attribute__((noreturn)) void exit(int code);
__attribute__((noreturn)) void _Exit(int code);
__attribute__((noreturn)) void abort(void);
int atexit(void (*fn)(void));
int atoi(const char* s);
long atol(const char* s);
double atof(const char* s);
float strtof(const char* s, char** end);
double strtod(const char* s, char** end);
long double strtold(const char* s, char** end);
int mblen(const char*, size_t);
int mbtowc(wchar_t*, const char*, size_t);
int wctomb(char*, wchar_t);
size_t mbstowcs(wchar_t*, const char*, size_t);
size_t wcstombs(char*, const wchar_t*, size_t);
long strtol(const char* s, char** end, int base);
unsigned long strtoul(const char* s, char** end, int base);
long long strtoll(const char* s, char** end, int base);
unsigned long long strtoull(const char* s, char** end, int base);
char* getenv(const char* name);
int setenv(const char* name, const char* value, int overwrite);
int unsetenv(const char* name);
int mkstemp(char* pattern);
int mkostemp(char* pattern, int flags);
int mkstemps(char* pattern, int suffixlen);
char* mkdtemp(char* pattern);
char* realpath(const char* path,char* resolved);
void qsort(void* base, size_t n, size_t size, int (*cmp)(const void*, const void*));
void* bsearch(const void* key, const void* base, size_t n, size_t size, int (*cmp)(const void*, const void*));
int abs(int v);
long labs(long v);
long long llabs(long long v);
div_t div(int numerator, int denominator);
ldiv_t ldiv(long numerator, long denominator);
lldiv_t lldiv(long long numerator, long long denominator);
int rand(void);
void srand(unsigned seed);
int rand_r(unsigned* seed);
#ifdef __cplusplus
}
#endif
#endif
