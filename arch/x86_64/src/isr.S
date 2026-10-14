// Заглушки всех 256 векторов прерываний и общий вход в C++.
// Раскладка кадра — struct InterruptFrame (idt.hpp).
//
// Код ошибки кладёт процессор только для векторов 8, 10–14, 17, 21
// (SDM Vol. 3A, табл. 6-1); для остальных заглушка кладёт 0, чтобы кадр
// всегда был одинаковым.

    .altmacro

.macro ISR_STUB n
    .align 16
isr_stub_\n:
    .if (\n == 8) || (\n >= 10 && \n <= 14) || (\n == 17) || (\n == 21)
    .else
    pushq   $0
    .endif
    pushq   $\n
    jmp     isr_common
.endm

    .text
.set i, 0
.rept 256
    ISR_STUB %i
    .set i, i + 1
.endr

isr_common:
    // Процессор выравнивает RSP на 16 перед кадром прерывания (SDM §6.14.2):
    // 5 слов процессора + код ошибки + вектор + 15 регистров = 22 слова,
    // поэтому перед call стек снова выровнен на 16.
    pushq   %rax
    pushq   %rbx
    pushq   %rcx
    pushq   %rdx
    pushq   %rsi
    pushq   %rdi
    pushq   %rbp
    pushq   %r8
    pushq   %r9
    pushq   %r10
    pushq   %r11
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15
    mov     %rsp, %rdi
    cld
    call    interrupt_dispatch
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %r11
    popq    %r10
    popq    %r9
    popq    %r8
    popq    %rbp
    popq    %rdi
    popq    %rsi
    popq    %rdx
    popq    %rcx
    popq    %rbx
    popq    %rax
    add     $16, %rsp       // вектор и код ошибки
    iretq

// Таблица адресов заглушек для init_idt().
.macro STUB_ADDR n
    .quad isr_stub_\n
.endm

    .section .rodata
    .align 8
    .globl isr_stub_table
isr_stub_table:
.set i, 0
.rept 256
    STUB_ADDR %i
    .set i, i + 1
.endr
