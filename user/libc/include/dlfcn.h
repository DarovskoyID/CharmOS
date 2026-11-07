#ifndef _DLFCN_H
#define _DLFCN_H
/* CharmOS is a static-first OS with no shared-object loader. Keep the POSIX
   surface for third-party portability, but every operation reports failure. */
#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_GLOBAL 0x100
#define RTLD_LOCAL 0
#define RTLD_NOLOAD 4
#define RTLD_DEFAULT ((void*)0)
#define RTLD_NEXT ((void*)-1)
typedef struct {
  const char* dli_fname;
  void* dli_fbase;
  const char* dli_sname;
  void* dli_saddr;
} Dl_info;
#ifdef __cplusplus
extern "C" {
#endif
void* dlopen(const char* file, int mode);
void* dlsym(void* handle, const char* name);
int dlclose(void* handle);
int dladdr(const void* address, Dl_info* info);
char* dlerror(void);
#ifdef __cplusplus
}
#endif
#endif
