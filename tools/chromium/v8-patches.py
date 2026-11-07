"""Native V8 platform selection for CharmOS (POSIX-compatible, no Rust)."""

def v8_patches():
    patches = {
        "v8/include/v8config.h": (
            "ec63d3457df7515c5cc1ac9e2adb2ccd847c77fa84aa56b5610ae21a3ce8c2d1", [
                ('#elif defined(__MVS__)\n# define V8_OS_POSIX 1\n# define V8_OS_ZOS 1',
                 '#elif defined(__charmos__)\n'
                 '# define V8_OS_POSIX 1\n'
                 '# define V8_OS_CHARMOS 1\n'
                 '# define V8_OS_STRING "charmos"\n\n'
                 '#elif defined(__MVS__)\n# define V8_OS_POSIX 1\n# define V8_OS_ZOS 1'),
            ]),
        # CharmOS registers SIGPROF (sigaction is a successful no-op) and defines
        # an x86-64-compatible mcontext_t. Reuse V8's Linux x64 register
        # extraction so the sampler compiles; the kernel never raises SIGPROF.
        "v8/src/libsampler/sampler.cc": (
            "cd3fc48c53040451ac5dd83f21bbee7edf1525bde1a0cee50aa60b19f7273277", [
                ('#if V8_OS_LINUX\n#if V8_HOST_ARCH_IA32',
                 '#if V8_OS_LINUX || V8_OS_CHARMOS\n#if V8_HOST_ARCH_IA32'),
            ]),
        # FlagList::FreezeFlags() mprotects the executable's .data page holding
        # v8_flags. CharmOS maps the process image through the ELF loader, not
        # through libc mmap(), so libc mprotect cannot change those pages yet.
        # The flags stay writable in memory (read-only by API convention); this
        # only skips the extra page-protection hardening.
        "v8/src/base/platform/platform-posix.cc": (
            "0e7ef6b332e90601531d588b0ea4c88cad22e696071ad74b09d27515bcd64a92", [
                ('void OS::SetDataReadOnly(void* address, size_t size) {\n'
                 '  CHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());\n'
                 '  CHECK_EQ(0, size % CommitPageSize());\n\n'
                 '  if (mprotect(address, size, PROT_READ) != 0) {\n'
                 '    FATAL("Failed to protect data memory at %p +%zu; error %d\\n", address, size,\n'
                 '          errno);\n'
                 '  }\n'
                 '}',
                 'void OS::SetDataReadOnly(void* address, size_t size) {\n'
                 '#if V8_OS_CHARMOS\n'
                 '  // No mprotect for image-mapped pages yet; skip page hardening.\n'
                 '  USE(address);\n'
                 '  USE(size);\n'
                 '#else\n'
                 '  CHECK_EQ(0, reinterpret_cast<uintptr_t>(address) % CommitPageSize());\n'
                 '  CHECK_EQ(0, size % CommitPageSize());\n\n'
                 '  if (mprotect(address, size, PROT_READ) != 0) {\n'
                 '    FATAL("Failed to protect data memory at %p +%zu; error %d\\n", address, size,\n'
                 '          errno);\n'
                 '  }\n'
                 '#endif\n'
                 '}'),
                ('bool OS::CanReserveAddressSpace() { return true; }',
                 'bool OS::CanReserveAddressSpace() {\n'
                 '#if V8_OS_CHARMOS\n'
                 '  // No fixed-address reservations; V8 uses its emulated\n'
                 '  // VirtualAddressSubspace backed by aligned page allocations.\n'
                 '  return false;\n'
                 '#else\n'
                 '  return true;\n'
                 '#endif\n'
                 '}'),
            ]),
        # Historical preparation stages, reversed below. Keep them recognizable
        # so preparation can safely migrate previously patched vendor trees.
        "v8/src/heap/heap.cc": (
            "ddcb62f457348321bb5955da2e7e0c4f2cd6291a8c021eda08de3e2b792bee09", [
                ('size_t Heap::NumberOfNativeContexts() {\n'
                 '  int result = 0;\n'
                 '  Tagged<Object> context = native_contexts_list();\n'
                 '  while (!IsUndefined(context)) {',
                 'size_t Heap::NumberOfNativeContexts() {\n'
                 '  int result = 0;\n'
                 '  Tagged<Object> context = native_contexts_list();\n'
                 '  while (IsContext(context)) {'),
                ('std::vector<Handle<NativeContext>> Heap::FindAllNativeContexts() {\n'
                 '  std::vector<Handle<NativeContext>> result;\n'
                 '  Tagged<Object> context = native_contexts_list();\n'
                 '  while (!IsUndefined(context)) {',
                 'std::vector<Handle<NativeContext>> Heap::FindAllNativeContexts() {\n'
                 '  std::vector<Handle<NativeContext>> result;\n'
                 '  Tagged<Object> context = native_contexts_list();\n'
                 '  while (IsNativeContext(context)) {'),
                ('std::vector<Tagged<WeakArrayList>> Heap::FindAllRetainedMaps() {\n'
                 '  std::vector<Tagged<WeakArrayList>> result;\n'
                 '  Tagged<Object> context = native_contexts_list();\n'
                 '  while (!IsUndefined(context)) {',
                 'std::vector<Tagged<WeakArrayList>> Heap::FindAllRetainedMaps() {\n'
                 '  std::vector<Tagged<WeakArrayList>> result;\n'
                 '  Tagged<Object> context = native_contexts_list();\n'
                 '  while (IsNativeContext(context)) {'),
            ]),
    }
    # Do not change GC invariants to hide a bad list head. Restore upstream
    # checks; a diagnostic in the allocation-failure path is not evidence that
    # arbitrary non-context objects are legitimate end-of-list sentinels.
    heap_stages = patches["v8/src/heap/heap.cc"][1]
    heap_stages.extend((after, before) for before, after in tuple(heap_stages))
    return patches
