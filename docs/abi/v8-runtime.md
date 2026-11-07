# Native V8 runtime (2026-11-05)

Status: guest-verified by the strict `m10/v8_runtime` gate on 2026-11-06. This is a real V8
isolate evaluating JavaScript, not a stub. It is **not** a browser: Blink,
content and the UI are not ported.

## Profile

GN arguments used by `tools/chromium/build-platform.sh --v8` / `--v8-graph`:

- `v8_enable_temporal_support=false` — removes the Rust `temporal_capi` crate.
- `v8_enable_pointer_compression=false` — no pointer-compression cage.
- `v8_enable_webassembly=false` — no wasm engine/trap handler.
- `v8_use_external_startup_data=false` — snapshot is embedded in the binary.
- `cppgc_enable_caged_heap=false`, `cppgc_enable_larger_cage=false` — Oilpan's
  ~32 GiB caged reservation cannot be represented by CharmOS VMOs.

The graph audit reports **1313 dependencies / 5854 compile commands and no
Rust** (`build-platform.sh --v8-graph`).

## Runtime

`v8_runtime` evaluates:

```js
1 + 2 * 3                                                        // 7
(function(){ var a=[1,2,3]; var s=0; for (var i of a) s+=i; return s; })() // 6
({x:4,y:5}).x + (function f(n){ return n<2?1:n*f(n-1); })(3)     // 10
```

The strict probe also checks ArrayBuffer/typed arrays, caught exceptions,
Map/JSON, a full collection after releasing context handles, and normal
isolate/engine/platform disposal followed by pthread_join. It uses upstream
weak-context-list checks, without the temporary permissive type guards.
The host check rejects late fatal errors even after a success line.

The engine runs on a pthread with an 8 MiB stack because kernel bootstrap
stacks are 64 KiB. Runtime flags: `--no-unmap-holes`, `--jitless`,
`--single-threaded`. The isolate receives an `ArrayBuffer::Allocator` and uses
V8's default x64 CodeRange reservation; ABI v1.21 lazy VMOs mean `PROT_NONE`
does not commit physical pages up front (`lazy-vmo.md`).

The guest scenario packages the stripped binary as
`build/user/user/v8_runtime.bin`. `tools/run.sh` packs every `*.elf` from the
user build into the initrd; a 60 MiB V8 entry makes UEFI/FAT boot under TCG
take minutes, so the guest loads this binary from the ext2 root instead. QEMU
uses the default 512 MiB guest: the 256 MiB JSDispatchTable reservation and the
CodeRange are lazy and commit only touched pages.

## Platform shims

- `src/base/platform/platform-posix.cc`: `SetDataReadOnly` is a no-op (the
  executable image is not a libc mapping, so `mprotect` on `v8_flags` is not yet
  supported); `OS::CanReserveAddressSpace()` returns false so V8 uses its
  emulated `VirtualAddressSubspace` instead of fixed-address reservations.
- `src/base/platform/platform-posix.cc`/`v8config.h`: `V8_OS_CHARMOS` is
  POSIX-like; the sampler reuses the Linux x64 `mcontext_t` extraction.
- `tools/chromium/platform/v8_platform_charmos.cc`: UTC timezone cache, empty
  shared-library list, no-op GC signalling/scheduling, stack-trace stubs and
  `powl` (LLVM numeric runtime provides `pow` only).
- libc additions: `ucontext.h`, `stack_t`/`sigaltstack`/`sigaction`,
  `pthread_getattr_np`/`pthread_attr_getstack`/`pthread_attr_getguardsize`/
  `pthread_kill`, `getrusage`/`RLIMIT_DATA`, `tmpfile`, `MAP_NORESERVE`,
  `tm_gmtoff`/`tm_zone`, `sys/syscall.h`.
- libc++ `<fstream>` and `ios.instantiations.cpp` key file streams on
  `_LIBCPP_HAS_LOCALIZATION` instead of `_LIBCPP_HAS_FILESYSTEM`; CharmOS keeps
  `std::filesystem` disabled.

## Limitations

- **No JIT.** `--jitless` is required because V8's x64 code range pre-commits its
  pages RWX, while CharmOS enforces W^X for VMO mappings. A future kernel policy
  (PKU or scoped RWX code pages) can lift this.
- SIGPROF registration returns ENOTSUP; the sampler detects the failed
  registration and remains unavailable. Alternate-stack installation and
  thread-directed signals also report ENOTSUP instead of fake success.
- getrusage reports ENOTSUP until real resource accounting is implemented;
  V8 handles the unavailable result. No zero-valued statistics are fabricated.
- Stack traces/`backtrace()` are unavailable.
- PROT_NONE is genuinely inaccessible (ABI v1.20), and the supported private
  anonymous MAP_FIXED replacement zeroes pages before denying access. ABI v1.21
  makes anonymous PROT_NONE reservations lazy. The strict V8 gate passes at the
  default 512 MiB guest. See `inaccessible-memory.md` and `lazy-vmo.md`.
- Partial munmap still does not release the selected range; the kernel cannot
  yet split a VMO mapping. Committed pages stay until the VMO is released.

## Reproduction

```
bash tools/chromium/with-cache.sh 1000 1000 bash tools/chromium/verify-v8.sh
```

This rebuilds the OS, regenerates the V8 graph, audits for Rust and static ELF,
and runs `m10/v8_runtime` in QEMU. The guest scenario uses a 300 s TCG timeout:
UEFI/FAT boot can exceed 100 s before the kernel reaches the shell. The test
exits via ACPI poweroff only after normal engine shutdown, the worker join, and
return to the shell. Use `--resume` to reuse a verified unchanged GN graph;
`CHARMOS_BUILD_JOBS` controls compilation parallelism.
