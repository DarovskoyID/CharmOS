#ifndef _PWD_H
#define _PWD_H
#include <sys/types.h>
/* CharmOS has no local account database; these records describe the process
   owner reported by the capability layer. */
struct passwd {
  char* pw_name;
  char* pw_passwd;
  uid_t pw_uid;
  gid_t pw_gid;
  char* pw_gecos;
  char* pw_dir;
  char* pw_shell;
};
#ifdef __cplusplus
extern "C" {
#endif
struct passwd* getpwuid(uid_t uid);
struct passwd* getpwnam(const char* name);
#ifdef __cplusplus
}
#endif
#endif
