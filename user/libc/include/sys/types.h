#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H
#include <stddef.h>
#include <stdint.h>
typedef int64_t ssize_t;
typedef int64_t off_t;
typedef int64_t off64_t;
typedef int pid_t;
typedef uint32_t mode_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef int64_t time_t;
typedef uint32_t useconds_t;
typedef uint64_t dev_t;
typedef uint64_t ino_t;
typedef uint64_t nlink_t;
typedef int64_t blksize_t;
typedef int64_t blkcnt_t;
typedef uint32_t socklen_t;
typedef uint32_t rdev_t;
/* BSD-совместимые целочисленные типы, используемые POSIX-кодом. */
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;
/* BSD source aliases; these do not define a host kernel ABI. */
typedef unsigned char u_char;
typedef unsigned int u_int;
#endif
