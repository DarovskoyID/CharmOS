#ifndef _GRP_H
#define _GRP_H
#include <sys/types.h>
/* CharmOS capabilities replace the group database. */
struct group {
  char* gr_name;
  char* gr_passwd;
  gid_t gr_gid;
  char** gr_mem;
};
#ifdef __cplusplus
extern "C" {
#endif
struct group* getgrgid(gid_t gid);
struct group* getgrnam(const char* name);
#ifdef __cplusplus
}
#endif
#endif
