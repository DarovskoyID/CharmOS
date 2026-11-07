# Lazy VMO backing — implemented, ABI v1.21

**Status: implemented and verified (2026-11-06).** The authoritative contract
is `lazy-vmo.md`; this file keeps the original design notes for history.

Goal: a large anonymous PROT_NONE reservation must not allocate its entire
physical backing. V8 already explicitly commits ranges by mprotect; no user
page-fault allocator is required for the first implementation.

Proposed ABI v1.21: add VmoReserve (next free syscall number, currently 84)
and SDK vmo_reserve(size). Existing VmoCreate remains eager and unchanged.
Reserve creates normal transferable VMO authority with a zeroed frame-index
array, charging the existing Job reservation quota. Zero frame entries mean
uncommitted pages, never permission to expose physical address zero.

Mapping with rights=0 only installs inaccessible ownership markers. Mapping
with access commits all missing pages first; restoring access to a subrange
commits only that subrange. Missing frames are zeroed before publication.
Allocation failure must roll back newly acquired frames before changing
permissions. Existing committed frames are preserved. All operations remain
under the existing IPC lock; no new allocation or Job lock inversion.

An inaccessible alias may still contain an uncommitted marker after another
alias commits the VMO. Protection restoration must refresh its frame address
from the VMO, not merely set Present on stale/zero PTE physical bits. Extend
the internal page-table operation and test this case. Destruction must skip
uncommitted frame entries and clear marker leaves without freeing frame zero.
Audit internal vmo_frames() (kernel/net/netif.cpp callers): commit requested
frames or return failure, never export an uncommitted physical zero.

libc should use Reserve for anonymous PROT_NONE only. Track a per-page
known-zero/inaccessible bit alongside protection: fixed anonymous replacement
can skip untouched zero reservations and only clear dirty runs. Otherwise the
current MAP_FIXED replacement would commit the entire untouched reservation.
Access restoration clears the known-zero marker conservatively; fixed
replacement sets it after zeroing and denying access. File snapshots are not
implicitly zero. Actual discard/free of committed pages is a later block.

Proof: sparse reserve/commit/alias/zero/rights/OOM rollback tests, then the
strict V8 GC/shutdown gate with QEMU RAM reduced from 2 GiB to 512 MiB.
Do not lower timeouts or claim the smaller profile until that guest passes.
