# Native application SDK 1.0

The public C++ SDK is engine-independent and uses the existing capability
model. Chromium adaptations live in tools/chromium/platform; other browser
engines can use the same headers without Chromium or a C++ standard library.
No new host services, dynamic loading or implicit inherited authority exist.

First implemented layer: charm/handle.hpp and charm/memory.hpp in user/rt/include.
UniqueHandle is move-only: adopt takes ownership, get borrows, release transfers,
duplicate explicitly requests a subset of rights. SharedMemory creates immutable
size, zeroed VMO objects; adoption queries the actual kernel object size rather
than trusting an IPC size field. Mapping owns the whole read-only or read/write
mapping and unmaps it on destruction; mappings survive ordinary handle closure.
Revocation retains its existing semantics and can invalidate mapped memory.
Mapping::release explicitly transfers unmapping responsibility to an engine
that already implements its own mapping ownership. No executable mapping is
available. create/adopt/map preserve their output on failure. Objects require
external synchronization when used by multiple threads.

## Additive syscall ABI 1.16

76 VmoSize(handle): RDI is an owned VMO capability, RDX is its immutable byte
size. The lookup requires no extra right beyond possessing the handle. It
does not grant mapping/read/write authority. BadHandle/WrongType propagate;
RDX is zero on failure. MMIO VMO sizes may be queried but holes are unchanged.

77 ThreadPriority(priority): 0 queries the calling thread's actual priority;
1..31 updates that same thread using the existing scheduler and ancestor Job
priority ceiling. RDX returns the actual priority, including clipping. Values
above 31 are InvalidArgs without changing it. This also works for bootstrap
threads, which have no exported Thread handle. It grants no authority over
other threads; ThreadSetPriority=58 retains its existing handle-based behavior.
RuntimeQuery adds selector 8 for the calling Job's effective ancestor priority
ceiling. Existing selectors 0..7 retain their meaning.

78 HandleRights(handle): returns the exact existing capability rights mask in
RDX. BadHandle returns zero. This is metadata about an already-owned handle,
never an authority lookup by object/PID. UniqueHandle::duplicate_same uses this
to preserve rights explicitly; duplicate(requested_rights) continues to narrow.

ABI 1.16 also permits ObjectUnbind on Process, Thread and Job one-shot binds,
with the same matching port/key and queued-packet rules as existing types.
This enables bounded, cancellable process waits without leaving port references.
charm/process.hpp exposes borrowed ProcessView: query/terminate/wait operate
only on the supplied capability, and wait uses an absolute monotonic deadline.
It never opens arbitrary processes by PID. Interrupted waits are reported and
cancelled; callers may retry at the original deadline. Exit output is only
written on successful completion. The borrowed handle must remain valid for
the call; concurrent independent watches can return Busy.

The SDK also retains existing public interfaces: charm/window.hpp for GUI,
libc poll for FD readiness, charm/sys.hpp for ports/channels and explicit
process capabilities, charm/libc.h for capability-limited spawning. Further
shared helpers must be exercised by real engine adapters before claiming a
complete browser SDK. GPU acceleration and browser-engine integration are not
implemented by these ownership helpers.

## Chromium process adapter

Native ProcessHandle is int64: zero invalid, -1 a current-process pseudo-handle,
and 1..UINT32_MAX real kernel capabilities. ProcessId remains diagnostic pid_t;
opening another process by PID is denied. Process owns real handles and closes
them without killing children; Duplicate preserves actual rights. Waiting uses
ProcessView deadlines/cancellation and keeps exit metadata available afterwards.
Whole-process priority changes, creation timestamps and executable-path/parent
queries are unavailable where the kernel exports no corresponding metadata.
GetPriority retains upstream's documented fallback, CreationTime its null value.

LaunchOptions gains native capability flags (CHARM_GIVE_*, default zero), Job,
priority and explicit handle/role/rights entries. Each transfer entry is first
duplicated with the requested subset and Transfer; originals always remain
owned by the caller. An untrusted child receives no implicit filesystem/network/
window services. Launch uses charm_spawn, never fork/exec or a loader service.
Arguments/environment must fit the existing startup limits; they are rejected,
not silently truncated. Unknown give bits, excess handles and failed authority
duplication fail before launch. FD remapping, POSIX pre-exec delegates/rlimit
changes/process groups and capturing command stdout are unsupported and fail
explicitly. Native child jobs replace process groups at engine integration.

## Startup capability lookup (SDK 1.1)

Application roles 0x10000..UINT32_MAX are local labels on capabilities already
transferred in the existing startup message, never global object identifiers.
libc preserves these labels. charm_take_start_handle(role) atomically removes
and returns the first matching extra capability, transferring ownership to the
caller. No match or a reserved role returns zero. A second take cannot return
the same capability. charm_extra_handle remains a borrowed positional lookup;
consumed slots return zero and other slot numbers remain stable.

Chromium shared-memory switches serialize application roles from 0x40000000,
not parent handle numbers or POSIX descriptors. LaunchOptions retains shared
ownership of duplicate VMO handles until launch; existing explicit transfer
entries determine child rights. The receiver takes only an actually received
startup capability and validates its VMO type, size and rights. Read-only
serialization removes Write authority before transfer. Malformed metadata
never grants authority or opens a process by PID.

## Native allocator accounting

charm_heap_allocated_bytes() returns the sum of backing block capacities of
live native libc allocations, including allocation headers, size-class rounding
and alignment backing storage. A zero-size allocation is counted using its
actual block. Reusing a free block counts it again; free removes it. This is
neither requested payload bytes nor physical resident memory. Free-list chunks
retained by the allocator are excluded. The counter is an atomic snapshot;
other threads may allocate immediately after the call. The function does not
allocate. Chromium GetMallocUsage uses this metric rather than fabricated
mallinfo or Linux allocator structures.

charm_open_fd_count() snapshots occupied native FD table slots under its lock,
including dup aliases and descriptors above a subsequently lowered soft limit.
Retained in-progress I/O descriptions whose last public FD was closed are not
counted as open descriptors. Neither function enumerates another process.
Native malloc tracing emits live_block_capacity only; absent RSS/virtual size
and requested payload counters are omitted rather than filled with zero.

## Channel streams (ABI 1.18)

charm/stream.hpp provides borrowed ChannelStream and wait_channel. Both peers
use payload-only messages no larger than 4096 bytes. read keeps unread bytes
in a fixed local buffer, write splits data into frames and waits on native
writable ports when the queue fills. StreamTransfer reports both status and
bytes already transferred; callers must not resend an accepted prefix after
timeout/error. Read returns Ok with zero bytes at EOF. All deadlines are
absolute; a retry does not extend them. No timer polling is used.

One reader and one writer may operate concurrently; use external serialization
for additional consumers/producers. The endpoint owner must outlive all I/O.
Shutdown may be concurrent, but reset/close/transfer require quiescent I/O and
no locally buffered bytes. Native Chromium SyncSocket enforces the buffered
transfer precondition. The SDK owns no capability implicitly, allocates no heap
for its receive buffer and does not expose queued handles as byte data.

Chromium's native message adapter uses typed capabilities and UniqueHandle
vectors at the old UnixDomainSocket include path. It copies transferable
handles with their actual rights, retaining caller ownership; send failure
closes temporary duplicates. Receive takes ownership only of actual received
capabilities. A receive buffer smaller than a message fails without consuming it. Numeric
Unix FDs, SCM_RIGHTS, Unix path sockets and sender PID credentials are not
emulated; credential requests fail ENOTSUP. Existing FD consumers must port
their ownership types explicitly before using this native transport.

## Additive syscall ABI 1.22

85 `VmoUnmapRange(addr, size)`: removes page-table leaves for
`[addr, addr+size)` inside one existing VMO mapping without deleting the
mapping record or freeing VMO frames. The range must be page-aligned and
contained in the mapping. Access to the removed pages faults; `mprotect` over
them fails. A later full `VmoUnmap` tears down the remaining mapping and
releases the VMO. libc partial `munmap` uses this and tracks per-page
`live` state so a hole cannot be protected or replaced.

## Additive syscall ABI 1.21

84 `VmoReserve(size)`: like `VmoCreate`, but physical frames are not allocated
until commit (an accessible map, `VmoProtect`, or kernel `vmo_frames`).
Returns a VMO handle with the same rights; eager allocation keeps the 1 GiB
limit, while a lazy reservation may be up to 64 GiB for allocator VA pools. The
Job memory quota is charged for the virtual size. Zero frame entries are never exposed as
physical address zero. See `lazy-vmo.md` for commit, alias and OOM rules.
`VmoCreate` keeps eager allocation and is unchanged.
