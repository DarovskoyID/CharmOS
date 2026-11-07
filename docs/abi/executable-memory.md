# Executable memory (W^X), ABI v1.19

Status: implemented and guest-verified (`runtime/runtime_none`, `runtime/runtime_cpu`).
This is a prerequisite for a JavaScript engine (V8) that compiles machine code at
run time. It adds no behaviour for existing calls; callers that never request
`kExec`/`PROT_EXEC` are unaffected.

## Rights

`abi::syscall::rights::kExec` (bit 5) authorises mapping a VMO executable.
`kVmoDefault` now includes it, so a process-local VMO created with `VmoCreate`
can be mapped executable. `kAll` is unchanged (channels/ports do not gain exec).

## Mapping

`VmoMap(handle, rights)` accepts `kExec`. Enforced invariants:

- `kRead` is required.
- **W^X:** `kWrite | kExec` in one request is rejected (`InvalidArgs`). A page is
  either writable or executable, never both.
- `kExec` maps the pages with the x86 NX bit clear; without it pages are NX.
- `kExec` is subject to the handle's rights like any other bit (`AccessDenied`).
- MMIO mappings reject `kExec` with `NotSupported` rather than clearing NX.

MMIO VMOs (`v->mmio`) are always NX; `kExec` on them is not meaningful.

## Changing protection

`VmoProtect(addr, size, rights)` (syscall 83) re-maps an existing VMO mapping in
the caller's address space, keeping the same virtual address. `addr` and `size`
must be page-aligned; the range must lie inside one mapping. Rules match
`VmoMap`: `kRead` required, `kWrite|kExec` rejected, MMIO rejected
(`NotSupported`). Because the pages were already visible, the kernel flushes the
TLB on all CPUs after the change.

The requested rights must remain a subset of the capability used to create
the mapping (`AccessDenied` otherwise). This ceiling remains in force after
the capability's handle is closed; closing the handle does not destroy its
existing mappings. Ranges that overflow or cross `kUserTop` are invalid.
libc tracks protection per page, so changing a subrange does not change the
recorded protection of neighbouring pages.
Protection updates replace existing leaf entries in place, without dropping
their present bit or allocating page-table frames. Other threads must not
observe a temporary unmapped page during a protection change. CPU-updated
accessed/dirty bits are preserved; TLB invalidation remains mandatory.

The intended JIT sequence is: map RW → write generated code → `VmoProtect` to RX
→ execute. The reverse (RX → RW) is not needed and is rejected by W^X only when
`kWrite` and `kExec` are requested together; the caller must drop one first.

## libc

- `mmap(..., PROT_READ|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS, ...)` maps RX.
- `mmap(..., PROT_READ|PROT_WRITE|PROT_EXEC, ...)` fails with `EINVAL` (W^X).
- `mprotect(addr, size, PROT_READ|PROT_EXEC)` flips a RW mapping to RX via
  `VmoProtect`, preserving the address.
- File mappings with `PROT_EXEC` are loaded into a temporary RW buffer, then
  re-mapped RX before returning.

## Verification

`user/tests/runtime_test.cpp` maps RW, writes a real `mov eax,42; ret`, flips to
RX, calls it and checks the result is 42, then confirms a W+X `mmap` fails with
`EINVAL`. Run by `runtime/runtime_none` and `runtime/runtime_cpu`.

## V8 address-space reservations (2026-11-05)

**Updated 2026-11-06, ABI v1.21:** anonymous private `PROT_NONE` uses lazy
`VmoReserve`; physical frames appear only on access restoration/commit. See
`lazy-vmo.md`. Zero mapping/protection rights still mean inaccessible pages
(`inaccessible-memory.md`); all nonzero protection requests retain the
Read/W^X/capability checks above.

The native V8 port exercises libc reservation behaviour that does not require
executable memory:

- `mmap(..., PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, ...)` installs non-present
  VMO leaves and reserves no physical frames: reads/writes/execution fault.
  Protection restoration retains the address and existing data; a page that was
  never committed becomes zero on first access.
- Anonymous/private `MAP_FIXED|PROT_NONE` inside an existing private libc
  mapping zeroes dirty replaced pages and makes them inaccessible. Neighbouring
  pages are unaffected. Known-zero runs are not committed; outside a tracked
  mapping this subset returns ENOMEM.
- A partial `munmap` removes just the selected leaves (`VmoUnmapRange`, ABI
  v1.22): surrounding pages stay usable and access to the hole faults. The VMO
  keeps frame ownership, so physical reclamation is still future work.
- `madvise(MADV_DONTNEED)` is a successful hint (no swap/demand paging).

V8 itself currently runs **without JIT** (`--jitless`): on x64 its code range is
pre-committed RWX, which contradicts the W^X rule above. Enabling JIT therefore
needs a kernel policy for PKU or scoped RWX code pages; the verified V8 gate
(`m10/v8_runtime`) evaluates JavaScript in the interpreter and passes at the
default 512 MiB guest RAM thanks to lazy reservations.
