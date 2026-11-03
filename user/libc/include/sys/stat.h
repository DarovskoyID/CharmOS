#ifndef _SYS_STAT_H
#define _SYS_STAT_H
#include <sys/types.h>
#define S_IFMT 0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_IFIFO 0010000
#define S_IFLNK 0120000
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001
#define S_IRWXU 0700
#define S_IRWXG 0070
#define S_IRWXO 0007
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
struct stat {
  off_t st_size;
  mode_t st_mode;
  time_t st_mtime;
  dev_t st_dev;
  ino_t st_ino;
  time_t st_atime, st_ctime;
};
#ifdef __cplusplus
extern "C" {
#endif
int stat(const char* path, struct stat* st);
int fstat(int fd, struct stat* st);
int fstatat(int directory,const char* path,struct stat* st,int flags);
int lstat(const char* path, struct stat* st);
int mkdir(const char* path, mode_t mode);
int chmod(const char* path,mode_t mode);
#ifdef __cplusplus
}
#endif
#endif
