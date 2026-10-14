// Граница ring 0 ↔ ring 3.
//
// SYSCALL (SDM Vol. 2B): RCX = RIP возврата, R11 = RFLAGS пользователя,
// RFLAGS маскирован IA32_FMASK (IF=0), RSP не меняется — стек ядра берём сами.
// GS после SWAPGS указывает на smp::Cpu: gs:8 — kernel_rsp, gs:16 — user_rsp.
//
// Кадр на стеке ядра (struct SyscallFrame в kernel/proc/syscall.hpp, по
// возрастанию адресов):
//   r15 r14 r13 r12 rbp rbx | r9 r8 r10 rdx rsi rdi rax | rip rflags rsp
// 16 слов: от вершины стека (кратной 16) RSP остаётся кратным 16 для call.

    .text
    .globl syscall_entry
    .type syscall_entry, @function
syscall_entry:
    swapgs
    mov     %rsp, %gs:16
    mov     %gs:8, %rsp
    pushq   %gs:16              // rsp пользователя
    pushq   %r11                // rflags
    pushq   %rcx                // rip
    pushq   %rax
    pushq   %rdi
    pushq   %rsi
    pushq   %rdx
    pushq   %r10
    pushq   %r8
    pushq   %r9
    pushq   %rbx
    pushq   %rbp
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15
    xor     %ebp, %ebp          // конец цепочки кадров для трассировки
    mov     %rsp, %rdi
    sti
    call    syscall_dispatch    // пишет статус и значение в кадр (rax, rdx)
    cli
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbp
    popq    %rbx
    popq    %r9
    popq    %r8
    popq    %r10
    popq    %rdx
    popq    %rsi
    popq    %rdi
    popq    %rax
    popq    %rcx                // rip (диспетчер проверил каноничность)
    popq    %r11                // rflags (диспетчер очистил привилегированные биты)
    popq    %rsp                // стек пользователя; прерывания запрещены
    swapgs
    sysretq

// [[noreturn]] void enter_user(uint64_t rip, uint64_t rsp)
// Первый вход потока в ring 3 (docs/abi/syscalls.md §7): все GPR = 0.
    .globl enter_user
    .type enter_user, @function
enter_user:
    cli
    pushq   $0x1b               // SS = данные пользователя | RPL 3
    pushq   %rsi                // RSP
    pushq   $0x202              // RFLAGS: IF
    pushq   $0x23               // CS = код пользователя | RPL 3
    pushq   %rdi                // RIP
    xor     %eax, %eax
    xor     %ebx, %ebx
    xor     %ecx, %ecx
    xor     %edx, %edx
    xor     %esi, %esi
    xor     %edi, %edi
    xor     %ebp, %ebp
    xor     %r8d, %r8d
    xor     %r9d, %r9d
    xor     %r10d, %r10d
    xor     %r11d, %r11d
    xor     %r12d, %r12d
    xor     %r13d, %r13d
    xor     %r14d, %r14d
    xor     %r15d, %r15d
    swapgs
    iretq

// uint64_t uaccess_copy(void* dst, const void* src, uint64_t n)
// 0 — скопировано, 1 — ошибка страницы. Обработчик #PF ядра, увидев RIP ==
// uaccess_fault_ip, переносит выполнение на uaccess_fixup. Диапазон адресов
// проверяет вызывающий (kernel/proc/uaccess.cpp).
    .globl uaccess_copy, uaccess_fault_ip, uaccess_fixup
    .type uaccess_copy, @function
uaccess_copy:
    mov     %rdx, %rcx
uaccess_fault_ip:
    rep movsb
    xor     %eax, %eax
    ret
uaccess_fixup:
    mov     $1, %eax
    ret
