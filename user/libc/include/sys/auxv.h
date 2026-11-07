#ifndef _SYS_AUXV_H
#define _SYS_AUXV_H
/* Minimal ELF auxiliary-vector surface. CharmOS user space does not expose
   AT_HWCAP/AT_PLATFORM, so queries report zero (no special CPU features). */
#define AT_NULL 0
#define AT_IGNORE 1
#define AT_EXECFD 2
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_PAGESZ 6
#define AT_BASE 7
#define AT_FLAGS 8
#define AT_ENTRY 9
#define AT_UID 11
#define AT_EUID 12
#define AT_GID 13
#define AT_EGID 14
#define AT_PLATFORM 15
#define AT_HWCAP 16
#define AT_CLKTCK 17
#define AT_SECURE 23
#define AT_HWCAP2 26
#ifdef __cplusplus
extern "C" {
#endif
unsigned long getauxval(unsigned long type);
#ifdef __cplusplus
}
#endif
#endif
