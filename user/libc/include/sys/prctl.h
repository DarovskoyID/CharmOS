#ifndef _SYS_PRCTL_H
#define _SYS_PRCTL_H
/* CharmOS process-control compatibility. Dumpability and names are accepted
   as no-ops because the capability model is the authority for introspection. */
#define PR_SET_PDEATHSIG 1
#define PR_GET_PDEATHSIG 2
#define PR_GET_DUMPABLE 3
#define PR_SET_DUMPABLE 4
#define PR_SET_NAME 15
#define PR_GET_NAME 16
#define PR_SET_NO_NEW_PRIVS 38
#ifdef __cplusplus
extern "C" {
#endif
int prctl(int option, ...);
#ifdef __cplusplus
}
#endif
#endif
