# Native static-libc mapping subset

This implements mmap/munmap over existing VMO syscalls; there is no Linux
syscall emulation or new kernel memory ABI. Pages are eagerly committed,
zeroed and non-executable. Maximum mapping size is the existing 1 GiB limit.

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
