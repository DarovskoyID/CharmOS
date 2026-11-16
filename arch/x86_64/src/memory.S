// SysV x86-64 byte-copy/zero primitives (DF is clear at kernel ABI entry).
// Kept here so generic core code contains no architecture-specific assembly.
    .text
    .globl charmos_x86_memcpy
    .type charmos_x86_memcpy, @function
charmos_x86_memcpy:
    mov %rdi, %rax
    mov %rdx, %rcx
    rep movsb
    ret
    .size charmos_x86_memcpy, .-charmos_x86_memcpy

    .globl charmos_x86_memset
    .type charmos_x86_memset, @function
charmos_x86_memset:
    mov %rdi, %r8
    mov %esi, %eax
    mov %rdx, %rcx
    rep stosb
    mov %r8, %rax
    ret
    .size charmos_x86_memset, .-charmos_x86_memset

    .section .note.GNU-stack,"",@progbits
