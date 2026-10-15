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

// [[noreturn]] void enter_user(uint64_t rip, uint64_t rsp, uint64_t arg)
// Первый вход потока в ring 3 (docs/abi/syscalls.md §7): RDI = arg
// (стартовый хендл), остальные GPR = 0.
    .globl enter_user
    .type enter_user, @function
enter_user:
    cli
    pushq   $0x1b               // SS = данные пользователя | RPL 3
    pushq   %rsi                // RSP
    pushq   $0x202              // RFLAGS: IF
    pushq   $0x23               // CS = код пользователя | RPL 3
    pushq   %rdi                // RIP
    mov     %rdx, %rdi          // стартовый хендл
    xor     %eax, %eax
    xor     %ebx, %ebx
    xor     %ecx, %ecx
    xor     %edx, %edx
    xor     %esi, %esi
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

// [[noreturn]] void resume_user(const abi::signal::SignalFrame* f)
// Полный возврат в ring 3 (signal_return): все GPR, RIP, RFLAGS и RSP из
// кадра (include/abi/signal.hpp: rax 0, rbx 8, rcx 16, rdx 24, rsi 32,
// rdi 40, rbp 48, rsp 56, r8..r15 64..120, rip 128, rflags 136). Значения
// уже проверены вызывающим.
    .globl resume_user
    .type resume_user, @function
resume_user:
    cli
    pushq   $0x1b               // SS
    pushq   56(%rdi)            // RSP
    pushq   136(%rdi)           // RFLAGS
    pushq   $0x23               // CS
    pushq   128(%rdi)           // RIP
    mov     0(%rdi), %rax
    mov     8(%rdi), %rbx
    mov     16(%rdi), %rcx
    mov     24(%rdi), %rdx
    mov     32(%rdi), %rsi
    mov     48(%rdi), %rbp
    mov     64(%rdi), %r8
    mov     72(%rdi), %r9
    mov     80(%rdi), %r10
    mov     88(%rdi), %r11
    mov     96(%rdi), %r12
    mov     104(%rdi), %r13
    mov     112(%rdi), %r14
    mov     120(%rdi), %r15
    mov     40(%rdi), %rdi
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
