#ifndef _SYS_RANDOM_H
#define _SYS_RANDOM_H
#include <sys/types.h>
#include <stddef.h>
#define GRND_NONBLOCK 1u
#ifdef __cplusplus
extern "C" {
#endif
ssize_t getrandom(void* buffer, size_t size, unsigned flags);
int getentropy(void* buffer, size_t size);
#ifdef __cplusplus
}
#endif
#endif
