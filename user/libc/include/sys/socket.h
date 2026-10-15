#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H
#include <sys/types.h>
#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2
typedef uint16_t sa_family_t;
struct sockaddr {
  sa_family_t sa_family;
  char sa_data[14];
};
#ifdef __cplusplus
extern "C" {
#endif
int socket(int domain, int type, int protocol);
int bind(int fd, const struct sockaddr* a, socklen_t len);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr* a, socklen_t* len);
int connect(int fd, const struct sockaddr* a, socklen_t len);
ssize_t send(int fd, const void* buf, size_t n, int flags);
ssize_t recv(int fd, void* buf, size_t n, int flags);
int shutdown(int fd, int how);
int setsockopt(int fd, int level, int name, const void* val, socklen_t len);
#ifdef __cplusplus
}
#endif
#endif
