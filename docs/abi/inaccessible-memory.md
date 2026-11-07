# Inaccessible VMO mappings — ABI v1.20

Implemented and guest-verified on 2026-11-06: runtime_none/runtime_cpu,
host test_paging, m4/ipc, and the full GC/clean-shutdown V8 gate.

This extends the existing VmoMap and VmoProtect requests with `rights=0`.
Previously valid requests retain their behaviour and capability checks.
Nonzero protection still requires Read; Write+Exec remains forbidden.

Zero protection reserves the VMO's virtual range without permitting reads,
writes or execution. VmoMap still requires Map authority. VmoProtect still
requires an existing mapping, page alignment, a contained range and the
original capability's ceiling for any subsequently restored permissions.
Closing a handle does not remove this ceiling. MMIO protection changes remain
unsupported.

On x86-64, inaccessible leaves have Present clear and an OS-owned marker in
software-available bit 9. Physical backing stays owned by the VMO. A marked
leaf cannot be overwritten by a new mapping; protection can restore it in
place, and unmap clears it. Protection changes invalidate TLBs on all CPUs.
User access faults; kernel user-copy operations fail through the existing
uaccess fixup path. No fake readable PROT_NONE alias is exposed.

libc anonymous/private mmap(PROT_NONE) and mprotect(PROT_NONE) use these
requests. Since ABI v1.21 these reservations are lazy (`VmoReserve`): only
touched pages acquire physical frames. Restoring access preserves data; it does
not imply discard or zero-on-recommit. Another alias may commit a frame after
this mapping installed a physical-zero marker; restoring access reloads the
physical address from the VMO (see `lazy-vmo.md`).

The supported MAP_FIXED subset is anonymous/private PROT_NONE replacement
inside one existing libc private mapping. It zeroes the replaced pages and
then makes them inaccessible, so later access restoration sees zeroes.
Untouched known-zero pages are not committed; only dirty runs are zeroed.
Neighbouring pages keep their contents and protection. Other MAP_FIXED modes
remain unsupported.
