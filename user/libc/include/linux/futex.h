#ifndef _LINUX_FUTEX_H
#define _LINUX_FUTEX_H
/* Linux futex operation constants. CharmOS does not expose a futex syscall;
   portable code paths that need it must fall back to pthread primitives. */
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_REQUEUE 3
#define FUTEX_CMP_REQUEUE 4
#define FUTEX_WAKE_OP 5
#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAKE_BITSET 10
#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_CLOCK_REALTIME 256
#define FUTEX_BITSET_MATCH_ANY 0xffffffff
#endif
