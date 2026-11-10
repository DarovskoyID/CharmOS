# Native libc file descriptors v2

Internal static-libc extension for Chromium; kernel handle/syscall ABI is
unchanged. All native apps are rebuilt with the updated libc.

Descriptors refer to shared open descriptions. dup/dup2/F_DUPFD share the
file offset and mutable O_APPEND/O_NONBLOCK state, retain one native handle
until the final reference disappears, and preserve the original access mode.
File read/write/lseek serialize offset changes. Positioned I/O leaves it
unchanged. In-progress operations retain their descriptions across close.
The descriptor table lock is not held while performing blocking I/O.

FD_CLOEXEC is a separate descriptor flag, cleared by dup and dup2 onto a
different descriptor. dup2(fd,fd) preserves flags. F_DUPFD_CLOEXEC sets it.
The existing capability-based charm_spawn transfers explicitly selected
handles; it does not introduce POSIX fork/exec descriptor-table inheritance.
No execve/posix_spawn compatibility is claimed here.

fcntl supports F_DUPFD, F_DUPFD_CLOEXEC, F_GETFD, F_SETFD, F_GETFL, F_SETFL.
F_SETFL accepts access-mode bits (ignored), O_APPEND and O_NONBLOCK; unknown
bits are EINVAL. Other operations are ENOTSUP; advisory locks are not faked.
Invalid descriptors are EBADF; invalid duplication minima are EINVAL.

O_NONBLOCK selects deadline zero for socket/console reads, socket accept and
TCP send; an unavailable result returns EAGAIN, while a partially accepted
nonblocking send returns that positive count without waiting for the rest.
The send deadline is carried by the v1.23 `socket_send_deadline` syscall
(socket.md §3.2); syscall 37 remains the blocking form. Nonblocking connect is
unsupported and returns ENOTSUP before starting a connection. Socket options
and nonzero send/recv message flags are unsupported, never silently reported
successful. Normal TCP connections continue through the existing native
transport.

## Channel-backed native pipes

pipe/pipe2 provide a byte stream over two native channel endpoints. Each
message carries at most PIPE_BUF=4096 bytes; writes up to that size are atomic.
Each endpoint has a shared open description, so dup preserves buffered reads,
nonblocking flags and lifetime. The reader acknowledges each dequeued message
on the reverse channel. Writers sleep waiting for acknowledgments when the
64-message queue fills; no timer-based busy polling is used. Nonblocking empty
reads/full writes fail EAGAIN; readers drain buffered data before EOF; writing
to a closed peer fails EPIPE (there is no SIGPIPE in the native signal ABI).
pipe2 accepts only O_CLOEXEC/O_NONBLOCK. Failed creation leaves the caller's
output array unchanged. POSIX fork/exec descriptor inheritance is unavailable.
fstat identifies pipes as S_IFIFO. The syscall/channel ABI is unchanged.

poll supports POLLIN/POLLOUT for files, console and native sockets/pipes using
ABI 1.14 watches. It retains descriptions across concurrent close, coalesces
duplicate handles, uses monotonic deadlines and cancels pending watches on
every return path. It supports at most 64 entries. POLLPRI is unsupported.
Socket stream EOF is readable; explicit socket POLLHUP classification is not
provided by the existing transport. A pipe with events=0 is ignored except
for an invalid descriptor, because the underlying channel cannot independently
watch peer closure while unread messages remain queued.

getrlimit/setrlimit(RLIMIT_NOFILE) expose and enforce a process-local soft
descriptor limit and monotonically reducible hard limit, initially 256/256.
All allocation/duplication paths obey the soft limit; existing higher-numbered
descriptors remain valid for I/O after lowering it. Raising the soft limit
within the hard bound is allowed, raising the hard bound is EPERM. _SC_OPEN_MAX
returns the current soft value. New capability-based spawned processes start
with their own limits; POSIX fork/exec limit inheritance is not claimed.
RLIMIT_STACK reports the fixed 64 KiB bootstrap main stack, separately from
explicit pthread VMO stack attributes; changing it is ENOTSUP. RLIMIT_CORE
is 0/0 because core-file dumping does not exist; enabling it is EPERM. Other
resource selectors fail EINVAL. These are native libc constants, not Linux
syscall resource numbers.

charm_spawn validates argument and environment entry counts as well as their
byte limits. Oversize startup data fails E2BIG, never silent truncation. Unknown
give bits, invalid priority and malformed extra-handle arrays fail EINVAL.
Too many startup handles fail before transferring any extras; authority-copy
failures close only copies created by the helper and preserve the caller's
originals. charm_wait uses ProcessView and cancels its one-shot bind on errors.

readv/writev use native struct iovec (pointer and size_t, 16 bytes) and up to
1024 vectors. They validate count/total length, gather or scatter through one
read/write call, and preserve its partial-transfer/error behavior. In particular,
a pipe writev totaling <=4096 bytes remains one atomic channel record. Larger
requests may fail ENOMEM when a contiguous staging buffer cannot be allocated.
They do not claim zero-copy kernel scatter/gather or ancillary FD transport.

basename/dirname implement the POSIX mutable-path lexical operations, including
empty/root/trailing-slash paths. They do not query the filesystem. fnmatch uses
byte-oriented C-locale matching with *, ?, bracket lists/ranges/negation and
POSIX character classes; FNM_PATHNAME, FNM_PERIOD and FNM_NOESCAPE are supported.
Collating/equivalence elements and non-C locale collation are unavailable.
# Channel readiness descriptors

charm_channel_watch_fd(endpoint, interests) creates a close-on-exec descriptor
for poll only, retaining an owned duplicate of a native channel with just the
requested Read=1/Write=2 rights. Zero/unknown masks fail EINVAL. The original
capability remains owned by the caller. A failed creation closes temporary
duplicates. The endpoint type is validated before import. close/dup/fcntl/poll
work normally; byte read/write and stat fail ENOTSUP because this descriptor
does not consume or reinterpret channel messages. Poll reports native channel
readable/writable/closure packets without dequeuing data. The descriptor must
be closed when its watcher is removed, before destroying an associated GUI
window. Existing one-subscription-per-endpoint limitations still apply.

This bridges native GUI/event channels into FD-based event loops without a
worker thread, polling timer, fabricated socket or transferred window authority.
