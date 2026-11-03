# Native libc nonlocal jumps

The C API follows C11 N1570 section 7.13 (`docs/specs/c11-n1570.pdf`).
This is an additive libc interface, without new kernel calls or ELF TLS.

On x86-64 a `jmp_buf` is one 64-byte, 8-byte-aligned record: eight consecutive
64-bit fields RBX, RBP, R12, R13, R14, R15, resumed RSP, resumed RIP. The saved
RSP points immediately above the setjmp return address. SysV AMD64 psABI 3.2.1
defines these integer callee-saved registers; caller-saved vector registers
are not part of the environment. The implementation is in `arch/x86_64/`.

`setjmp` has the compiler's `returns_twice` attribute and initially returns
zero. `longjmp` is noreturn and returns its second argument to the saved
invocation, substituting one for zero. A jump buffer must remain in the same
live thread and enclosing function invocation. Changes to automatic,
non-volatile locals between save and restore make their values indeterminate.
Neither operation transfers ownership or unwinds C++ destructors.

Floating-point environment, open files and other abstract-machine state
remain as at the call to longjmp, per N1570 7.13.2.1 paragraphs 3 and footnote
249. In particular, the jump does not reset rounding mode or exception flags.
Signal masks are not saved; this API does not provide sigsetjmp/siglongjmp.

The buffer contains executable control state and must not come from untrusted
input. Invalid, expired, cross-thread or modified buffers have undefined
behavior as in the C API; they are not a kernel isolation mechanism.
