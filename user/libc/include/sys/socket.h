#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H
#include <sys/types.h>
#include <sys/uio.h>
#define AF_UNSPEC 0
#define AF_UNIX 1
#define AF_INET 2
#define AF_INET6 10
#define PF_UNSPEC AF_UNSPEC
#define PF_INET AF_INET
#define PF_INET6 AF_INET6
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_NONBLOCK 0x800
#define SOCK_CLOEXEC 0x80000
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SO_TYPE 3
#define SO_ERROR 4
#define SO_BROADCAST 6
#define SO_SNDBUF 7
#define SO_RCVBUF 8
#define SO_KEEPALIVE 9
#define SO_LINGER 13
#define SO_PEERCRED 17
#define SO_TIMESTAMP 29
#define SO_TIMESTAMPNS 35
#define MSG_PEEK 2
#define MSG_DONTWAIT 0x40
#define MSG_NOSIGNAL 0x4000
#define MSG_CTRUNC 8
#define MSG_TRUNC 0x20
#define MSG_CONFIRM 0x800
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2
#define SOMAXCONN 128
typedef uint16_t sa_family_t;
struct sockaddr {
  sa_family_t sa_family;
  char sa_data[14];
};
struct sockaddr_storage {
  sa_family_t ss_family;
  unsigned char __storage[118];
  uint64_t __alignment;
};
struct linger { int l_onoff,l_linger; };
/* Ancillary data (control messages). CharmOS не передаёт SCM_RIGHTS через
   сокеты — дескрипторы/капабилити ходят по каналам ядра. Типы объявлены,
   чтобы POSIX-код компилировался; sendmsg/recvmsg с control-данными вернут
   ошибку или проигнорируют их честно. */
struct cmsghdr {
  size_t cmsg_len;
  int cmsg_level;
  int cmsg_type;
};
struct ucred { pid_t pid; uid_t uid; gid_t gid; };
#define SCM_RIGHTS 1
#define SCM_CREDENTIALS 2
#define SCM_TIMESTAMP 29
#define CMSG_ALIGN(len) (((len) + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1))
#define CMSG_SPACE(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(len))
#define CMSG_LEN(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))
#define CMSG_DATA(cmsg) ((unsigned char*)(cmsg) + CMSG_ALIGN(sizeof(struct cmsghdr)))
#define CMSG_FIRSTHDR(m) \
  ((m)->msg_controllen >= sizeof(struct cmsghdr) ? (struct cmsghdr*)(m)->msg_control : (struct cmsghdr*)0)
#define CMSG_NXTHDR(m, c) \
  ((c) == (struct cmsghdr*)0 || (char*)(c) + CMSG_ALIGN((c)->cmsg_len) + sizeof(struct cmsghdr) > (char*)(m)->msg_control + (m)->msg_controllen \
      ? (struct cmsghdr*)0 : (struct cmsghdr*)((char*)(c) + CMSG_ALIGN((c)->cmsg_len)))
struct msghdr {
  void* msg_name;
  socklen_t msg_namelen;
  struct iovec* msg_iov;
  int msg_iovlen;
  void* msg_control;
  socklen_t msg_controllen;
  int msg_flags;
};
#if defined(__cplusplus)
static_assert(sizeof(sockaddr_storage)==128 && alignof(sockaddr_storage)==8);
#endif
#ifdef __cplusplus
extern "C" {
#endif
int socket(int domain, int type, int protocol);
int socketpair(int domain, int type, int protocol, int sv[2]);
int bind(int fd, const struct sockaddr* a, socklen_t len);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr* a, socklen_t* len);
int connect(int fd, const struct sockaddr* a, socklen_t len);
ssize_t send(int fd, const void* buf, size_t n, int flags);
ssize_t recv(int fd, void* buf, size_t n, int flags);
ssize_t sendmsg(int fd, const struct msghdr* msg, int flags);
ssize_t recvmsg(int fd, struct msghdr* msg, int flags);
int shutdown(int fd, int how);
int setsockopt(int fd, int level, int name, const void* val, socklen_t len);
int getsockopt(int fd,int level,int name,void* value,socklen_t* length);
int getsockname(int fd,struct sockaddr* address,socklen_t* length);
int getpeername(int fd,struct sockaddr* address,socklen_t* length);
ssize_t sendto(int fd,const void* data,size_t size,int flags,const struct sockaddr* address,socklen_t length);
ssize_t recvfrom(int fd,void* data,size_t size,int flags,struct sockaddr* address,socklen_t* length);
#ifdef __cplusplus
}
#endif
#endif
