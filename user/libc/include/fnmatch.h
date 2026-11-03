#ifndef _FNMATCH_H
#define _FNMATCH_H
#define FNM_NOMATCH 1
#define FNM_PATHNAME 1
#define FNM_NOESCAPE 2
#define FNM_PERIOD 4
#ifdef __cplusplus
extern "C" {
#endif
int fnmatch(const char* pattern, const char* string, int flags);
#ifdef __cplusplus
}
#endif
#endif
