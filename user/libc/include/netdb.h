#ifndef CHARMOS_NETDB_H
#define CHARMOS_NETDB_H
#include <sys/socket.h>
#define AI_PASSIVE 1
#define AI_CANONNAME 2
#define AI_NUMERICHOST 4
#define AI_V4MAPPED 8
#define AI_ALL 16
#define AI_ADDRCONFIG 32
#define AI_NUMERICSERV 1024
#define NI_NUMERICHOST 1
#define NI_NUMERICSERV 2
#define NI_NOFQDN 4
#define NI_NAMEREQD 8
#define NI_DGRAM 16
#define NI_MAXHOST 1025
#define NI_MAXSERV 32
#define EAI_BADFLAGS -1
#define EAI_NONAME -2
#define EAI_AGAIN -3
#define EAI_FAIL -4
#define EAI_FAMILY -6
#define EAI_SOCKTYPE -7
#define EAI_SERVICE -8
#define EAI_MEMORY -10
#define EAI_SYSTEM -11
#define EAI_OVERFLOW -12
#define EAI_NODATA -5
struct addrinfo {
  int ai_flags,ai_family,ai_socktype,ai_protocol;
  socklen_t ai_addrlen;
  struct sockaddr* ai_addr;
  char* ai_canonname;
  struct addrinfo* ai_next;
};
#ifdef __cplusplus
extern "C" {
#endif
int getaddrinfo(const char* node,const char* service,const struct addrinfo* hints,struct addrinfo** result);
void freeaddrinfo(struct addrinfo* result);
const char* gai_strerror(int error);
int getnameinfo(const struct sockaddr* address,socklen_t length,char* host,socklen_t host_size,
                char* service,socklen_t service_size,int flags);
#ifdef __cplusplus
}
static_assert(sizeof(addrinfo)==48 && __builtin_offsetof(addrinfo,ai_addr)==24);
#endif
#endif
