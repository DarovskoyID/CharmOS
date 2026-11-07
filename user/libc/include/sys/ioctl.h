#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H
#include <sys/types.h>
/* ioctl поддерживает только ограниченный набор запросов; остальные честно
   возвращают ENOTSUP. */
#define FIONREAD 0x541B
#define FIONBIO 0x5421
#define TIOCGWINSZ 0x5413
struct winsize { unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel; };
#ifdef __cplusplus
extern "C" {
#endif
int ioctl(int fd, unsigned long request, ...);
#ifdef __cplusplus
}
#endif
#endif
