# Lazy VMO backing — ABI v1.21 (implemented)

Status: implemented and guest-verified 2026-11-06. The strict V8 gate (full
collection, `Isolate::Dispose`, platform dispose, clean join) passes in QEMU
with the default 512 MiB guest, and `runtime_none`/`runtime_cpu`, host
`test_paging` and `m4/ipc` pass. This replaces the earlier draft; the design
below is kept as the contract.

## Goal

A large anonymous `PROT_NONE` reservation must not allocate its entire physical
backing. V8 already commits ranges explicitly by `mprotect`; no demand page
fault handler is required. `VmoCreate` remains eager and unchanged.

## Syscall

`VmoReserve(size)` (syscall 84, ABI v1.21) returns normal transferable VMO
authority (`kVmoDefault`) whose frame-index array is zero. `size` has the same
alignment and 1 GiB limit as `VmoCreate`. The Job reservation quota is charged
for the virtual size, exactly like `VmoCreate`; committing pages does not charge
again, so physical use can never exceed the charged reservation.

Zero frame entries mean **uncommitted**, never permission to expose physical
address zero. Frame zero is permanently reserved by the physical allocator.

## Commit rules

- `VmoMap(handle, rights=0)` only installs inaccessible ownership markers; no
  frames are acquired.
- `VmoMap(handle, rights!=0)` commits every missing page before publishing the
  mapping.
- `VmoProtect(addr, size, rights!=0)` commits only missing pages in that range.
- Commits are all-or-nothing: frames are allocated and zeroed into a temporary
  array and published only after every allocation succeeds. Existing committed
  frames are preserved; an OOM leaves the VMO unchanged.
- Missing frames are zeroed before publication.
- `vmo_frames()` (kernel consumers such as the network driver) commits the VMO
  before exporting physical addresses, so hardware never receives zero as a
  frame address. Failure is reported as `NoMemory`.
- Destroying a VMO frees only nonzero frame entries.

## Aliases

An inaccessible alias may still hold a zero/uncommitted marker after another
alias commits the VMO. Restoring access always reloads the physical address
from the VMO and rewrites the leaf (`protect_4k` alone is insufficient). This
is covered by a host page-table test and a guest alias test.

## libc

- Anonymous private `mmap(PROT_NONE)` uses `VmoReserve`.
- Per-page `zeroed` tracking marks pages that have never been writable.
  `MAP_FIXED|PROT_NONE` replacement inside a private mapping:
  - does not commit untouched known-zero pages;
  - makes dirty runs writable, zeroes them, then denies access;
  - leaves neighbouring pages and their protection intact.
- Making a range writable clears its known-zero bits. Restoring access does not
  release or discard committed pages.
- File snapshots and non-`PROT_NONE` anonymous mappings keep eager `VmoCreate`.

Actual discard/free of committed pages, arbitrary `MAP_FIXED`, shared mappings
and partial `munmap` remain future work; partial `munmap` is still a documented
no-op.

## Proof

- host `test_paging`: inaccessible marker → remap to a committed frame, then a
  second commit with a different frame replaces the stale physical address.
- `runtime/runtime_none`, `runtime/runtime_cpu`:
  `runtime: lazy VMO reservation and alias commit ok` (two mappings of one
  reserved VMO, mutually visible writes after commit; 32 MiB sparse reservation
  with a small fixed replacement).
- `m10/v8_runtime`: strict V8 lifecycle at the default 512 MiB QEMU RAM, with
  the x64 default CodeRange reservation (no 64 MiB workaround).

Partial `munmap` (ABI v1.22) removes leaves without reclaiming the VMO's
frames; committed pages are released only when the VMO is released. Physical
discard/zero-on-recommit remains future work.
