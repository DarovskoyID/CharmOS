#ifndef _STDIO_H
#define _STDIO_H
#include <stdarg.h>
#include <stddef.h>
#include <sys/types.h>
typedef struct __file FILE;
#define EOF (-1)
#define BUFSIZ 1024
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#ifdef __cplusplus
extern "C" {
#endif
extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;
int printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
int fprintf(FILE* f, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
int dprintf(int fd, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
int sprintf(char* buf, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
int snprintf(char* buf, size_t n, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
int vprintf(const char* fmt, va_list ap);
int vfprintf(FILE* f, const char* fmt, va_list ap);
int vsnprintf(char* buf, size_t n, const char* fmt, va_list ap);
int vdprintf(int fd, const char* fmt, va_list ap);
int puts(const char* s);
int fputs(const char* s, FILE* f);
int fputc(int c, FILE* f);
int putc(int c, FILE* f);
int putchar(int c);
int fgetc(FILE* f);
int getc(FILE* f);
int getchar(void);
char* fgets(char* buf, int n, FILE* f);
FILE* fopen(const char* path, const char* mode);
FILE* fdopen(int fd, const char* mode);
int fclose(FILE* f);
size_t fread(void* buf, size_t size, size_t n, FILE* f);
size_t fwrite(const void* buf, size_t size, size_t n, FILE* f);
int fflush(FILE* f);
int fseek(FILE* f, long off, int whence);
long ftell(FILE* f);
int feof(FILE* f);
int ferror(FILE* f);
void clearerr(FILE* f);
int fileno(FILE* f);
void perror(const char* s);
int setvbuf(FILE* f, char* buf, int mode, size_t size);
#ifdef __cplusplus
}
#endif
#endif
