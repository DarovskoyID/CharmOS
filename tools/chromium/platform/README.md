# CharmOS platform bring-up

The pinned Chromium tree has five hash-checked downstream patches:
GN target selection, build/build_config.h, compiler-rt path selection, and
the libc++/libc++abi no-exception runtime configuration. __charmos__ identifies the
target; IS_CHARMOS/IS_POSIX are true, IS_LINUX/IS_ANDROID are false.
Linux remains the host for code generators, not the target userspace ABI.

prepare-platform.py verifies the original file hashes, retains originals,
and copies this small downstream overlay into build/charmos in the private
source cache. Unknown local vendor changes are rejected rather than replaced.

The toolchain wrapper uses the archive's Clang 24, the Generic ELF target,
CharmOS C headers and bundled libc++ headers. Target links are static and use
the existing native libc and linker script. Shared-library/PIE output is not
implemented and is rejected. An ELF audit also rejects interpreter, dynamic,
and compiler TLS segments/sections: the native loader does not support these.
The archive's compiler has an empty release stamp, so clang_base_path explicitly
registers it as a custom compiler; no matching official stamp is fabricated.
Missing libraries or symbols remain errors.

platform_smoke is a bootstrap executable with explicit upstream algorithm and
verbose_abort units. It tests platform identity, vector/sort/unique_ptr/atomics
and UTF-8 byte preservation in a guest. It does not link the full libc++.
build-platform.sh --full-runtime additionally attempts the ordinary upstream
libc++ target; locale/math/filesystem prerequisites remain incomplete.
The ABI runtime uses upstream cxa_noexception, keeps internal RTTI, and omits
exception storage/unwinding and the guards/virtual handlers owned by native libc.
base_port selects actual //base dependencies and is a separate bring-up goal;
a generated GN graph alone is not proof that Chromium base or a browser runs.
No source is compiled with __linux__ to bypass a missing CharmOS backend.
