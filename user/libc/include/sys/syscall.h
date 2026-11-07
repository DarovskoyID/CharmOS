#ifndef _SYS_SYSCALL_H
#define _SYS_SYSCALL_H
#include <sys/types.h>
#include <unistd.h>
/* CharmOS: минимальная syscall(2). Нужна V8 для __NR_gettid; __NR_mseal
   (sealing памяти) не поддерживается и возвращает ENOSYS. */
#define __NR_gettid 186
#define __NR_mseal 462
#define __NR_futex 202
#define SYS_futex __NR_futex
#ifdef __cplusplus
extern "C" {
#endif
long syscall(long number, ...);
#ifdef __cplusplus
}
#endif
#endif
