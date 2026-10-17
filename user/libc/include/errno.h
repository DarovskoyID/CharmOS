#ifndef _ERRNO_H
#define _ERRNO_H
/* Коды ошибок libc CharmOS (подмножество POSIX); соответствие статусам
   ядра — user/libc/src/internal.hpp (errno_from). */
#ifdef __cplusplus
extern "C" {
#endif
int* __errno_location(void);
#ifdef __cplusplus
}
#endif
#define errno (*__errno_location())
#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define E2BIG 7
#define ENOEXEC 8
#define EBADF 9
#define ECHILD 10
#define EAGAIN 11
#define EWOULDBLOCK EAGAIN
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EBUSY 16
#define EEXIST 17
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define EMFILE 24
#define ENOTTY 25
#define ENOSPC 28
#define ESPIPE 29
#define EPIPE 32
#define ERANGE 34
#define ENOSYS 38
#define ENOTEMPTY 39
#define ENOTSOCK 88
#define EILSEQ 84
#define EOVERFLOW 75
#define EADDRINUSE 98
#define ENETUNREACH 101
#define ECONNRESET 104
#define ETIMEDOUT 110
#define ECONNREFUSED 111
#endif
