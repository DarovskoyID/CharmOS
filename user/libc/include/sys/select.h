#ifndef _SYS_SELECT_H
#define _SYS_SELECT_H
#include <sys/types.h>
#include <sys/time.h>
/* fd_set: bitmap of file descriptors (FD_SETSIZE = 1024). */
#define FD_SETSIZE 1024
typedef struct { uint64_t bits[FD_SETSIZE / 64]; } fd_set;
#define __FD_BIT(fd) ((uint64_t)1 << ((fd) % 64))
#define FD_ZERO(set) __builtin_memset((set), 0, sizeof(fd_set))
#define FD_SET(fd, set) ((set)->bits[(fd) / 64] |= __FD_BIT(fd))
#define FD_CLR(fd, set) ((set)->bits[(fd) / 64] &= ~__FD_BIT(fd))
#define FD_ISSET(fd, set) (((set)->bits[(fd) / 64] & __FD_BIT(fd)) != 0)
#ifdef __cplusplus
extern "C" {
#endif
int select(int nfds, fd_set* read, fd_set* write, fd_set* except, struct timeval* timeout);
#ifdef __cplusplus
}
#endif
#endif
