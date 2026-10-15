#ifndef _UNISTD_H
#define _UNISTD_H
#include <stddef.h>
#include <sys/types.h>
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#ifdef __cplusplus
extern "C" {
#endif
ssize_t read(int fd, void* buf, size_t n);
ssize_t write(int fd, const void* buf, size_t n);
ssize_t pread(int fd, void* buf, size_t n, off_t off);
ssize_t pwrite(int fd, const void* buf, size_t n, off_t off);
int close(int fd);
off_t lseek(int fd, off_t off, int whence);
int dup(int fd);
int dup2(int fd, int to);
int unlink(const char* path);
int rmdir(const char* path);
int chdir(const char* path);
char* getcwd(char* buf, size_t size);
int isatty(int fd);
int fsync(int fd);
int ftruncate(int fd, off_t size);
unsigned sleep(unsigned seconds);
int usleep(unsigned usec);
pid_t getpid(void);
extern char** environ;
__attribute__((noreturn)) void _exit(int code);
#ifdef __cplusplus
}
#endif
#endif
