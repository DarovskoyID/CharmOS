#ifndef _SYS_UTSNAME_H
#define _SYS_UTSNAME_H
struct utsname {
  char sysname[65], nodename[65], release[65], version[65], machine[65], domainname[65];
};
#ifdef __cplusplus
extern "C" {
#endif
int uname(struct utsname* value);
#ifdef __cplusplus
}
#endif
#endif
