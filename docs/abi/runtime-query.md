# RuntimeQuery — syscall ABI 1.13

Extension for the native C++ Chromium port. Existing syscall numbers,
structures and meanings are unchanged. RuntimeQuery=72 accepts selector RDI;
on success RDX contains a uint64 result. No handles or pointers are accepted.
Only the calling process/thread is observable; no extra capability is needed.

| Selector | Result |
|---|---|
| 0 | Calling process's real, nonzero kernel process ID |
| 1 | Calling thread's real, nonzero kernel scheduler ID |
| 2 | Process CPU nanoseconds charged across its threads, including exited threads |
| 3 | Calling thread's charged CPU nanoseconds |
| 4 | Conservative process CPU accounting period in nanoseconds (scheduler tick) |

Other selectors: InvalidArgs, RDX=0. Kernel IDs are not handles and grant no
authority. IDs are assigned by existing kernel counters, not user pointers or
fixed values. They remain stable for the lifetime of their objects.

CPU values reuse the scheduler's existing Job charging boundaries, including
kernel execution on behalf of the process. Reading CPU selectors checkpoints
the calling thread's current running interval. Other CPUs' active intervals
become visible on their next normal accounting boundary. Sleeping/blocked time
is excluded. Process readings are monotonic, aggregate simultaneous execution
on multiple CPUs, and retain already charged time from exited threads.
The reported accounting period describes this coarse remote-CPU visibility;
these values do not substitute elapsed wall time for CPU usage.

Native libc getpid/gettid query selectors 0/1 and report EOVERFLOW if a kernel
ID cannot be represented by its existing pid_t. clock_gettime gains
CLOCK_PROCESS_CPUTIME_ID=2 and CLOCK_THREAD_CPUTIME_ID=3. CLOCK_BOOTTIME=7 uses
the existing monotonic source while CharmOS has no system suspend. Raw clock
ID 4 is reserved but unsupported; a caller must handle EINVAL. Clock constants
are local libc API values, not Linux syscall emulation.

ABI 1.14 adds selector 5: total physical frames managed by the allocator
(initial usable-memory budget, excluding firmware/kernel reservations);
6: current free managed frames, sampled under the PMM lock; 7: online CPU
count from the initialized SMP registry. Frame size is 4096 bytes. Selectors
0..4 retain their existing meaning; selector 4 is a duration, not a CPU count.

ABI 1.16 adds selector 8: the calling Job's effective ancestor maximum thread
priority (1..31). See application-sdk.md for the current-thread query/update.
