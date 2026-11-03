# Native unnamed counting semaphores

`semaphore.h` adds process-local unnamed semaphores backed by native futexes.
`sem_t` is 16 bytes, aligned to four: uint32 count at 0, waiter count at 4,
initialization marker at 8, reserved zero at 12. It has stable storage while
in use and must not be copied. The layout belongs to native libc, not glibc.
There is no kernel ABI change. Sources: `docs/specs/posix/README.md`.

sem_init accepts pshared=0 and counts 0..SEM_VALUE_MAX (INT_MAX). Nonzero
pshared fails ENOTSUP; excessive counts fail EINVAL before modifying storage.
Named semaphores and cross-process shared semaphore storage are not offered.
Initialization, destruction and reinitialization require external lifetime
synchronization. Destroy with registered waiters returns EBUSY.

sem_wait consumes one available token with acquire ordering, otherwise sleeps
on the actual count word. sem_trywait never sleeps and returns EAGAIN for zero.
sem_post increments with release ordering, fails EOVERFLOW without mutation at
the maximum, and wakes one futex waiter. Posting uses no heap, locks, or libc
thread state on its success path, so native signal handlers can post.
Wake is unconditional to avoid a registration/count race and lost wakeups.
The native scheduler does not expose SCHED_FIFO/RR selection guarantees.

sem_timedwait uses an absolute CLOCK_REALTIME timespec. Immediate acquisition
precedes timeout validation. Otherwise invalid nanoseconds or an unrepresentable
deadline give EINVAL, expired deadlines ETIMEDOUT, and interrupted waits EINTR.
The current immutable native realtime offset permits conversion to a monotonic
futex deadline. If realtime adjustment is introduced, timed waits must be
updated to follow it. Repeated spurious wakes do not extend the deadline.

sem_getvalue reports a nonnegative token snapshot; it does not subtract waiter
counts. Null/uninitialized handles and null output pointers give EINVAL.
Success returns zero without changing errno; failure returns -1 with errno.
Cancellation is not advertised by native pthreads and is not added here.
