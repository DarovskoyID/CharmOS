# Native Chromium platform: C++-only profile

M10 internal port; no new syscall ABI in this document. Native binaries use
CharmOS capabilities, own libc and the Generic ELF toolchain. Linux identity,
host libc, ELF dynamic loading and an ELF TLS loader are not supported by this
profile. Missing platform operations must not report successful emulation.
Compiler thread_local uses native runtime storage with -femulated-tls;
see compiler-tls.md for its descriptor and destructor contract.

The initial core target is //base, with real C++ JSONReader. Optional Rust
bindings, Rust logging registration and upstream test suites requiring those
bindings are excluded. Native probes remain separate runnable guest tests.
The newer ICU4X-based //base:i18n component is currently unavailable: a C++
ICU4C replacement is required before enabling clients of that component.
This is an unfinished browser port, not a reduced target claimed as a browser.

Perfetto must identify CharmOS separately, without defining Linux/FreeBSD or
other foreign OS flags. Use the embedded tracing client; system daemon IPC
is disabled until there is a native transport. Clock readings must use real
CharmOS clocks. Unsupported CPU clocks are not wall time measurements.

ICU can use its upstream stdio file-data backend rather than POSIX mmap.
Native paths and file contents remain UTF-8/bytes; data file load failures
must propagate normally. Unicode data must actually be loaded before claiming
locale services work.

Native libc calendar extension retains the existing 36-byte tm layout and
UTC-only Gregorian contract. localtime/localtime_r use UTC, tm_isdst is zero;
tzname entries are UTC, timezone and daylight are zero. tzset accepts no TZ,
empty TZ, UTC, UTC0 and GMT0. Other TZ values leave UTC selected and set
errno=EINVAL. This is a documented fixed-UTC subset, not a timezone database.
mktime normalizes civil components within Gregorian years 1..9999, rejects
positive tm_isdst, reports EOVERFLOW outside that range and writes the
normalized tm on success. difftime returns the difference in seconds.
These additions do not fabricate timezone support or CPU accounting.

timegm uses the same normalization and year bounds as mktime, but always
interprets the input as UTC and ignores tm_isdst. On success it writes the
normalized fields with tm_isdst=0. Null input fails with EINVAL; out-of-range
input fails with EOVERFLOW without changing the input. A return value of -1
can also represent 1969-12-31 23:59:59 UTC; errno distinguishes failure.

## Native filesystem additions

mkstemp/mkostemp and mkdtemp require a writable template ending in six X
characters. Each attempt uses getentropy; creation uses the existing atomic
exclusive-create operation, with at most 256 collision retries. Entropy or
filesystem failures propagate; no predictable counter fallback is used.
mkstemp returns an O_RDWR descriptor, mkostemp also accepts O_APPEND,
O_CLOEXEC and O_NONBLOCK. Directory creation uses the same exclusive path.
No Unix mode-bit security is claimed: capabilities remain authoritative.

access(F_OK) checks existence; R_OK/W_OK check that an open with the requested
capability succeeds. The filesystem has no executable permission metadata:
X_OK fails with ENOTSUP rather than claiming a permission check occurred.
Unknown bits fail with EINVAL. useconds_t is uint32_t, matching the existing
usleep parameter ABI.

strptime parses the fixed C locale and updates the supplied tm only after a
successful parse, returning the first unconsumed input byte. Supported
directives: Y/C/y, m/b/B/h, d/e, a/A, j/w/u, H/I/M/S/p, n/t/%, F/D/R/T/r,
c/x/X and C-locale E/O modifiers. Complete dates derive weekday/year-day
and reject invalid Gregorian dates. The y pivot is 68/69 (2068/1969).
Z accepts UTC/GMT; z accepts Z or +/-0000, since tm has no offset field.
Other zones/directives and malformed input fail with nullptr/EINVAL without
modifying tm; this does not claim support for locale databases or ISO week
date parsing. Perfetto clock snapshots contain the native realtime,
monotonic and boottime clocks, without fabricated coarse/raw clock aliases.
CCTZ's libc timezone accessor uses the documented fixed UTC contract.

uname returns native identity in six 65-byte fields (390 bytes, machine
offset 260). sysname=CharmOS, release=abi-MAJOR.MINOR reflects the actual
compiled syscall ABI, version describes the native capability kernel and
machine names the compiled target. No nodename is configured: nodename is
"(none)" and domainname is empty. Null output fails EFAULT. getpagesize and
sysconf(_SC_PAGESIZE) return the ABI's 4096-byte page size. The two processor
count selectors use RuntimeQuery(7). _SC_PHYS_PAGES and _SC_AVPHYS_PAGES use
the managed-frame counts in RuntimeQuery(5/6); unknown selectors fail EINVAL.

Abseil ELF-memory-image/VDSO discovery is disabled on this static-ELF platform.
The optional POSIX failure-signal handler is excluded from the native Abseil
aggregate; it remains unavailable, rather than installing a dummy handler.

Perfetto PagedMemory has a native VMO implementation. It uses zeroed, eagerly
committed, read/write non-executable VMO pages and unmaps the exact allocation
on destruction. Its two guards use the kernel's unmapped inter-VMO gaps;
the fixed native executable image is linked below the VMO arena. The maximum
single allocation is the existing 1 GiB VMO limit. kMayFail returns an invalid
object on error; kDontCommit permits eager commitment and does not introduce
virtual overcommit. The optional discard hint returns false, since VMO v1.1
cannot discard individual pages. Guest subprocess tests must fault on both
guard pages; passing compilation alone is not evidence of memory protection.

Chromium base uses native monotonic/realtime/thread-CPU clocks. Its clock
identifier appends CHARMOS_CLOCK_MONOTONIC without renumbering existing
identifiers. LowResolutionNow samples the normal monotonic clock because no
separate coarse source exists. Calendar conversion retains libc's fixed UTC
contract and does not assume non-POSIX tm extension fields. Random byte
generation uses the kernel entropy service through getentropy in <=256-byte
chunks, including the allocation-avoiding path; it does not open /dev/urandom.

Chromium platform thread IDs use actual gettid, while references/handles remain
pthread objects. Default worker stacks are 2 MiB and use real libc attributes.
Thread diagnostic names are maintained in Chromium's registry; this does not
rename kernel threads. Native priorities map background/utility/default/
presentation/audio/realtime-audio to 4/8/16/20/24/28, using ThreadPriority
and the real ancestor Job ceiling. Effective-priority queries read the kernel;
CanChangeThreadType checks the Job ceiling. These are priority levels, without
hard realtime deadline guarantees. Cross-thread priority override requests
remain unsupported and return false. No POSIX nice/UID model is invented.

Native shared-memory regions use real capability handles and the public SDK.
Read-only conversion duplicates a strictly reduced capability and closes the
writable owner; existing mappings remain alive according to native VMO rules.
TakeOrFail checks the real size and exact rights. The mapper retains whole-VMO
mappings behind offset views and releases the real base even after MapAt trims
a subpage prefix. Mapping registry access is synchronized. This is shared
memory, not private file copies.

Native SysInfo reports kernel ABI identity, online CPUs, managed physical RAM
and the user virtual-address ceiling. Disk-space/hardware-drive details and
per-page residency are unavailable rather than measured as zero. Optional
discard returns false without marking a segment purged. Dynamic library loads
fail ENOTSUP in the static profile; naming follows GN conventions. Process and
launch contracts are in application-sdk.md. A PID never grants authority.
# Native diagnostics

The static image exports __charm_text_start/end from its linker script for
address classification. ModuleCache reports only that real text extent; it
has no dynamic loader or synthetic build-id. Other addresses need explicitly
registered runtime modules. This profile discards unwind tables and may omit
frame pointers: native stack collection records one immediate return address,
not a guessed complete walk. Output contains raw addresses. Installing Unix
fault handlers and asynchronous stack sampling are unavailable; the upstream
non-ChromeOS StackSampler returns nullptr. Signal-only thread suspension and
register-context copying are excluded rather than compiled against invented
Linux ucontext structures. The kernel retains its own native fault reporting.

Native ProcessMetrics measures self CPU with RuntimeQuery(2), allocator live
capacity with charm_heap_allocated_bytes and descriptor count under the native
FD-table lock. It rejects cross-process creation and reports unsupported
resident memory/idle-wakeup measurements explicitly. Heap capacity is not RSS.
