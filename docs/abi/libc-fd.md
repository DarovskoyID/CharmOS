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

O_NONBLOCK selects deadline zero for socket/console reads and socket accept;
an unavailable result returns EAGAIN. Nonblocking connect is unsupported and
returns ENOTSUP before starting a connection. Socket options and nonzero
send/recv message flags are unsupported, never silently reported successful.
Normal TCP connections continue through the existing native transport.
