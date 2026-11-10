# Native static-libc mapping subset

**Current extension and review status (2026-11-05):** executable mappings
and real protection changes are described in `executable-memory.md` (v1.19).
Protection is tracked per page. The original subset below documents the
baseline, not all current behaviour.

**ABI v1.22, guest-verified 2026-11-06:** partial `munmap` now removes the
selected page leaves (`VmoUnmapRange=85`). Surrounding pages stay usable and
access to the hole faults; `mprotect`/`MAP_FIXED` over a hole fail with
`ENOMEM`. The VMO keeps ownership of the frames (physical reclamation/discard is
still future work).

**ABI v1.21, guest-verified 2026-11-06:** anonymous private `PROT_NONE`
uses `VmoReserve`, so a large reservation has no physical backing until access
is restored. See `lazy-vmo.md`. `MAP_FIXED|PROT_NONE` replacement skips
untouched known-zero runs and only commits+zeroes dirty runs.

**ABI v1.20, guest-verified 2026-11-06:** PROT_NONE now uses non-present
leaves. In-range anonymous/private MAP_FIXED|PROT_NONE zeroes the replaced
pages before denying access, preserving neighbouring pages. See
`inaccessible-memory.md`.

Partial `munmap` removes the selected leaves (ABI v1.22), but the VMO keeps the
frames: committed pages are released only when the VMO itself is released.
Physical discard is still future work.

`MADV_DONTNEED` currently accepts the request without releasing or zeroing
pages. No reclamation or zero-on-recommit guarantee is provided by this port.

## Portable-runtime compatibility surface (2026-11-06)

The C++ Chromium port needs a wider POSIX header surface than the original
libc exposed. These additions are declarations and honest stubs, not new kernel
ABI:

- `sys/stat.h`: `struct stat` now carries `st_nlink`, `st_uid`, `st_gid`,
  `st_rdev`, `st_blksize` and `st_blocks` (96 bytes on x86-64). `nlink` is 1,
  ownership is 0, `blksize` is 4096 and `blocks` is `size/512`; the native
  filesystem does not store ownership or permissions.
- `fchmod` reports `ENOTSUP` (authority is capability based); `fchown` is an
  accepted no-op; `utimes`/`utime` validate the path and accept the request
  without persisting timestamps.
- `sys/file.h`: `flock` accepts advisory operations as no-ops; intra-process
  ordering still comes from capability handles.
- `sys/auxv.h`: `getauxval` returns 0 (no AT_HWCAP/AT_PLATFORM exposure).
- `sys/mman.h`: `mlock`/`munlock`/`mlockall`/`munlockall` succeed because
  committed CharmOS pages are already resident.
- `sched.h`: `SCHED_*`, `struct sched_param`, priority queries, and
  `pthread_setschedparam`/`sched_setscheduler` (`ENOTSUP` for policy changes).
- `dlfcn.h`: no dynamic loader; `dlopen`/`dlsym` report `ENOSYS`.
- `alloca.h`, `byteswap.h`, `utime.h` and `FILENAME_MAX`/`rand_r` complete the
  portable C surface used by media and JavaScript dependencies.


## Original baseline

This implements mmap/munmap over existing VMO syscalls; there is no Linux
syscall emulation or new kernel memory ABI. Pages are eagerly committed,
zeroed and non-executable. Ordinary mappings remain capped at 1 GiB; lazy
anonymous `PROT_NONE` reservations may be up to 64 GiB (no frames committed).

Anonymous MAP_PRIVATE mappings support PROT_READ and PROT_READ|PROT_WRITE.
An address is only a hint and the kernel chooses the returned address.
MAP_FIXED, MAP_SHARED, executable and inaccessible reservations are unsupported
(ENOTSUP). Unknown flags/protections and zero lengths fail EINVAL. Overflow or
allocation failure is ENOMEM. No feature silently succeeds with weaker access.

File-private mappings copy a requested readable regular-file range into a VMO
using positioned I/O without changing its open-description offset. They are
private snapshots, with writes never propagated to the file. Requests past
the end of the file fail EINVAL: delayed SIGBUS beyond EOF is not emulated.
Offsets must be nonnegative page multiples. Read-only mappings are filled via
a temporary writable alias which is removed before the returned read-only
alias is created. Writable aliases do not remain accessible on success.

munmap supports a complete mapping at its original base address, with length
rounded to pages. Partial unmaps fail ENOTSUP; unknown mappings fail EINVAL.
mprotect can confirm existing protections, including on a subrange, but cannot
change them (ENOTSUP). madvise supports only the optional MADV_NORMAL hint;
discard hints fail ENOTSUP. msync validates a private mapped range and flags;
private snapshots have no backing writes to flush. No shared-file or VMO
permission-transition support is claimed by these libc APIs.
