#ifndef _UNISTD_H
#define _UNISTD_H
#include <stddef.h>
#include <sys/types.h>
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#define _SC_PAGESIZE 1
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define _SC_NPROCESSORS_ONLN 2
#define _SC_NPROCESSORS_CONF 3
#define _SC_PHYS_PAGES 4
#define _SC_AVPHYS_PAGES 5
#define _PC_NAME_MAX 1
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
int pipe(int descriptors[2]);
int pipe2(int descriptors[2], int flags);
int unlink(const char* path);
int unlinkat(int directory,const char* path,int flags);
int rmdir(const char* path);
int chdir(const char* path);
char* getcwd(char* buf, size_t size);
int isatty(int fd);
int fsync(int fd);
int ftruncate(int fd, off_t size);
int ftruncate64(int fd, off_t size);
#define _SC_OPEN_MAX 6
unsigned sleep(unsigned seconds);
int usleep(useconds_t usec);
int access(const char* path, int mode);
long pathconf(const char* path,int name);
int symlink(const char* target,const char* path);
ssize_t readlink(const char* path,char* buffer,size_t capacity);
int chown(const char* path,uid_t owner,gid_t group);
int lchown(const char* path,uid_t owner,gid_t group);
long sysconf(int name);
int getpagesize(void);
pid_t getpid(void);
pid_t gettid(void);
uid_t geteuid(void);
uid_t getuid(void);
gid_t getgid(void);
gid_t getegid(void);
int getpeereid(int fd, uid_t* euid, gid_t* egid);
int gethostname(char* name, size_t size);
void sync(void);
int pause(void);
int getentropy(void* buffer, size_t size);
extern char** environ;
__attribute__((noreturn)) void _exit(int code);
#ifdef __cplusplus
}
#endif
#endif
