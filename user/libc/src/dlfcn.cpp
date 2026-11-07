// Static CharmOS has no dynamic loader. Provide the POSIX dl* surface so
// portable code links, while honestly reporting that no object can be loaded.
#include <dlfcn.h>
#include <errno.h>

extern "C" void* dlopen(const char* file, int mode) {
  (void)file;
  (void)mode;
  errno = ENOSYS;
  return nullptr;
}

extern "C" void* dlsym(void* handle, const char* name) {
  (void)handle;
  (void)name;
  errno = ENOSYS;
  return nullptr;
}

extern "C" int dlclose(void* handle) {
  (void)handle;
  errno = ENOSYS;
  return -1;
}

extern "C" int dladdr(const void* address, Dl_info* info) {
  (void)address;
  (void)info;
  errno = ENOSYS;
  return 0;
}

extern "C" char* dlerror(void) { return const_cast<char*>("dynamic loading is not supported"); }
