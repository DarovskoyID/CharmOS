# Compiler thread-local storage — native emulated TLS

The pinned Clang 24 supports -femulated-tls: C/C++ thread-local accesses call
__emutls_get_address instead of assuming an ELF PT_TLS loader or a Linux TCB.
This profile uses that lowering for every Chromium target compilation. Native
libc consumers also inherit it from the CMake c target; raw rt-only programs
without a ThreadBlock do not acquire a TLS runtime implicitly. Native
libc retains its existing FS-based ThreadBlock and introduces no Linux identity,
dynamic loader or ELF TLS program header. audit-elf continues rejecting PT_TLS,
TLS data sections; the linker also rejects unresolved native STT_TLS layout.
This is real thread-local storage implemented by
the native runtime; it does not replace thread_local with process globals.

Source ABI: pinned third_party/llvm/compiler-rt/lib/builtins/emutls.c,
__emutls_control definition and __emutls_get_address entry point. On the native
64-bit Generic ELF target a control has four 8-byte words: size at 0, alignment
at 8, runtime index/address at 16 and initial template pointer at 24; size=32.
Each variable has its own descriptor; nonzero initial data has a template,
otherwise storage is zeroed. The native runtime keys allocations by descriptor
address and leaves the optional index word unused. The pinned compiler does
not directly inspect that runtime cache word. Descriptor metadata is immutable
after static link; dynamic images/common registration are outside this profile.

Each ThreadBlock owns a list of aligned allocations. The first access on a
thread allocates and initializes its own object; later accesses return the
same address. No global key or finite pthread-key slot is consumed per variable.
Separate threads never share a TLS instance, even when initialized concurrently.
Runtime allocation failure aborts in this no-exception profile. Storage remains
valid through C++ thread-local destructors and pthread key destructor iterations.

__cxa_thread_atexit and __cxa_thread_atexit_impl register a per-thread LIFO list.
The static profile does not unload DSOs, so dso_symbol needs no loader pin.
Worker exit runs C++ TLS destructors before pthread key destructors, processes
new C++ registrations made by key destructors, then frees emulated storage.
Normal process exit runs main-thread TLS destructors before ordinary atexit
and global/static destructors. _Exit, abort, forced thread/process termination
retain their existing immediate termination semantics and run no destructors.

No kernel structure, syscall number or public pthread layout changes. Private
ThreadBlock fields are an internal libc/runtime contract. The libc++abi native
target does not select its generic pthread fallback, avoiding duplicated TLS lifetime
management; the native runtime owns both compiler entry points explicitly.
