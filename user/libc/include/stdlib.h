#ifndef _STDLIB_H
#define _STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 0x7fffffff
#ifdef __cplusplus
extern "C" {
#endif
void* malloc(size_t size);
void* calloc(size_t n, size_t size);
void* realloc(void* p, size_t size);
void free(void* p);
__attribute__((noreturn)) void exit(int code);
__attribute__((noreturn)) void _Exit(int code);
__attribute__((noreturn)) void abort(void);
int atexit(void (*fn)(void));
int atoi(const char* s);
long atol(const char* s);
long strtol(const char* s, char** end, int base);
unsigned long strtoul(const char* s, char** end, int base);
long long strtoll(const char* s, char** end, int base);
unsigned long long strtoull(const char* s, char** end, int base);
char* getenv(const char* name);
int setenv(const char* name, const char* value, int overwrite);
int unsetenv(const char* name);
void qsort(void* base, size_t n, size_t size, int (*cmp)(const void*, const void*));
int abs(int v);
long labs(long v);
int rand(void);
void srand(unsigned seed);
#ifdef __cplusplus
}
#endif
#endif
