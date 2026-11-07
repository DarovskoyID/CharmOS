#ifndef _RESOLV_H
#define _RESOLV_H
#include <netinet/in.h>
/* Native resolver snapshot from NetInfo: actual DNS server, no invented
   search domains. Name resolution itself uses the kernel DnsResolve call. */
#define MAXNS 3
#define RES_MAXNS MAXNS
#define MAXDNSRCH 6
#define RES_INIT 0x00000001
#define RES_DEBUG 0x00000002
#define RES_AAONLY 0x00000004
#define RES_USEVC 0x00000008
#define RES_IGNTC 0x00000020
#define RES_RECURSE 0x00000040
#define RES_DEFNAMES 0x00000080
#define RES_STAYOPEN 0x00000100
#define RES_DNSRCH 0x00000200
#define RES_ROTATE 0x00000004
#define RES_USE_INET6 0x10000000
struct __res_state {
  int retrans;
  int retry;
  uint32_t options;
  int nscount;
  struct sockaddr_in nsaddr_list[MAXNS];
  unsigned ndots;
  char defdname[256];
  char* dnsrch[MAXDNSRCH];
  char __pad[512];
};
typedef struct __res_state* res_state;
#ifdef __cplusplus
extern "C" {
#endif
extern struct __res_state _res;
int res_ninit(struct __res_state* state);
int res_nclose(struct __res_state* state);
void res_ndestroy(struct __res_state* state);
int res_init(void);
#ifdef __cplusplus
}
#endif
#endif
