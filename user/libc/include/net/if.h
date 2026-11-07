#ifndef _NET_IF_H
#define _NET_IF_H
#include <sys/socket.h>
#define IFNAMSIZ 16
#define IF_NAMESIZE IFNAMSIZ
#ifdef __cplusplus
extern "C" {
#endif
unsigned if_nametoindex(const char* name);
char* if_indextoname(unsigned index, char* name);
#ifdef __cplusplus
}
#endif
#endif
