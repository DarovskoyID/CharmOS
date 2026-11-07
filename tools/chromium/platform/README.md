# CharmOS platform bring-up

The pinned Chromium tree has hash-checked downstream patches for GN target
selection, native OS integration, compiler-rt paths and the libc++/libc++abi
no-exception runtime configuration. __charmos__ identifies the
target; IS_CHARMOS/IS_POSIX are true, IS_LINUX/IS_ANDROID are false.
Linux remains the host for code generators, not the target userspace ABI.

prepare-platform.py verifies the original file hashes, retains originals,
and copies this small downstream overlay into build/charmos in the private
source cache. Unknown local vendor changes are rejected rather than replaced.

The toolchain wrapper uses the archive's Clang 24, the Generic ELF target,
CharmOS C headers and bundled libc++ headers. Target links are static and use
the existing native libc and linker script. Shared-library/PIE output is not
implemented and is rejected. An ELF audit also rejects interpreter, dynamic,
and ELF TLS segments/sections: the native loader does not support these.
Compiler thread_local instead uses -femulated-tls and native libc-owned storage
with per-thread initialization, alignment and destructor cleanup.
The archive's compiler has an empty release stamp, so clang_base_path explicitly
registers it as a custom compiler; no matching official stamp is fabricated.
Missing libraries or symbols remain errors.

platform_smoke is a bootstrap executable with explicit upstream algorithm and
verbose_abort units. It tests platform identity, vector/sort/unique_ptr/atomics
and UTF-8 byte preservation in a guest. It does not link the full libc++.
build-platform.sh --full-runtime links the ordinary upstream libc++ target;
the native runtime has passed in the guest, including threads, numeric/locale
support, files, shared memory, process capabilities and channel streams.
The ABI runtime uses upstream cxa_noexception, keeps internal RTTI, and omits
exception storage/unwinding and the guards/virtual handlers owned by native libc.
base_port selects actual //base dependencies and is a separate bring-up goal;
a generated GN graph alone is not proof that Chromium base or a browser runs.
No source is compiled with __linux__ to bypass a missing CharmOS backend.

Native engine adapters are ordinary C++ sources in this overlay: process
launch/wait, VMO-backed shared memory, file allocation, event pump, system
information, channel messages and synchronous streams. The common ownership,
memory, process and stream helpers live in user/rt/include/charm, independent
of Chromium. Read their ABI contracts before importing capabilities.

--base copies and audits base_runtime only after a successful link. The QEMU
base_runtime fixture exercises real task loops, memory/process transfer, files
and IPC in CharmOS. It does not turn a generated graph or compiled archive into
a completed browser. Browser rendering, engine IPC consumers, Ozone integration
and a usable browser window still require their own native implementation.

--graphics builds an actual Skia CPU/window integration probe. Its own graph
audit checks that the C++ codecs/FreeType/HarfBuzz closure contains no Rust.
The m10/graphics_runtime fixture drives a real WM window through Chromium's UI
task loop and native input, and saves a screenshot. It is not a browser.
Six double hyperbolic entrypoints absent from this archive's LLVM libc are
backed by its pinned Chromium fdlibm implementation with native C errno
handling. The additional source/header hashes and license are retained.

`verify-browser-foundation.sh` keeps one native profile, compiles Ozone and
Mojo together, audits both ELF files and then runs their guest fixtures plus
the libc network regression. This avoids repeated graph/compile cycles for
individual source edits. `prepare-platform.py --check-only` validates pinned
patch contexts without changing a live vendor tree.

Dependency-audit receipts hash every GN input and recursively included Ninja
manifest, arguments, generation stamp and omitted-Rust report. Unchanged graphs
can reuse the verified result after C++ edits. Changed or unrecognized inputs
require the full audit; metadata timestamps alone never authorize cache reuse.

The independent SDK also exposes `charm/network.hpp`: real interface/DNS
snapshots and IPv4 name resolution through an explicitly borrowed Net
capability. Other browser engines can use it without Chromium or host libc.
