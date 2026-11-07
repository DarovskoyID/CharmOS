#ifndef _SYS_FILE_H
#define _SYS_FILE_H
/* Advisory file locking surface used by portable databases and runtimes.
   CharmOS authority is capability based, so locks are process-local no-ops. */
#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8
#ifdef __cplusplus
extern "C" {
#endif
int flock(int fd, int operation);
#ifdef __cplusplus
}
#endif
#endif
