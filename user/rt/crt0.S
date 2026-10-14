// Точка входа процесса (docs/abi/syscalls.md §7): RSP выровнен на 16, все
// регистры 0. Вызывает charm_main() и завершает процесс с её результатом.

    .text
    .globl _start
    .type _start, @function
_start:
    xor     %ebp, %ebp
    call    charm_main
    mov     %rax, %rdi
    mov     $2, %eax            // ProcessExit
    syscall
    ud2
