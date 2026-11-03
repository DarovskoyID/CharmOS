#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <sys/resource.h>
#include <unistd.h>
#include "internal.hpp"

static_assert(sizeof(utsname)==390 && __builtin_offsetof(utsname,machine)==260);
extern "C" int uname(struct utsname* value) {
  if (!value) return libc::fail_errno(EFAULT);
  utsname result{};
  auto version=charm::abi_version();
  if (!version.ok()) return libc::fail(version.status);
  strcpy(result.sysname,"CharmOS");
  strcpy(result.nodename,"(none)");
  snprintf(result.release,sizeof result.release,"abi-%u.%u",
           static_cast<unsigned>(version.value>>16),
           static_cast<unsigned>(version.value&0xffff));
  strcpy(result.version,"CharmOS capability kernel");
#if defined(__x86_64__)
  strcpy(result.machine,"x86_64");
#else
#error Define the native target machine name for this architecture
#endif
  *value=result;
  return 0;
}
extern "C" int getpagesize(void) { return 4096; }
extern "C" long sysconf(int name) {
  if (name==_SC_OPEN_MAX) {
    rlimit limit;
    if (getrlimit(RLIMIT_NOFILE,&limit)!=0) return -1;
    return static_cast<long>(limit.rlim_cur);
  }
  if (name==_SC_PAGESIZE) return getpagesize();
  if (name==_SC_NPROCESSORS_ONLN || name==_SC_NPROCESSORS_CONF) {
    auto result=charm::runtime_query(7);
    if (!result.ok()) return libc::fail(result.status);
    return static_cast<long>(result.value);
  }
  if (name==_SC_PHYS_PAGES || name==_SC_AVPHYS_PAGES) {
    auto result=charm::runtime_query(name==_SC_PHYS_PAGES ? 5 : 6);
    if (!result.ok()) return libc::fail(result.status);
    return static_cast<long>(result.value);
  }
  return libc::fail_errno(EINVAL);
}
