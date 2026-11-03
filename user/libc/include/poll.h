#ifndef _POLL_H
#define _POLL_H
typedef unsigned long nfds_t;
struct pollfd { int fd; short events, revents; };
#define POLLIN 0x001
#define POLLPRI 0x002
#define POLLOUT 0x004
#define POLLERR 0x008
#define POLLHUP 0x010
#define POLLNVAL 0x020
#define POLLRDNORM POLLIN
#define POLLWRNORM POLLOUT
#ifdef __cplusplus
extern "C" {
#endif
int poll(struct pollfd* descriptors, nfds_t count, int timeout_ms);
#ifdef __cplusplus
}
#endif
#endif
