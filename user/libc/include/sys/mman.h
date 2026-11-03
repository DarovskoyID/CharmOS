#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H
#include <stddef.h>
#include <sys/types.h>
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_FAILED ((void*)-1)
#define MADV_NORMAL 0
#define MADV_DONTNEED 4
#define MS_ASYNC 1
#define MS_INVALIDATE 2
#define MS_SYNC 4
#ifdef __cplusplus
extern "C" {
#endif
void* mmap(void* address, size_t size, int protection, int flags, int fd, off_t offset);
int munmap(void* address, size_t size);
int mprotect(void* address, size_t size, int protection);
int madvise(void* address, size_t size, int advice);
int msync(void* address, size_t size, int flags);
#ifdef __cplusplus
}
#endif
#endif
