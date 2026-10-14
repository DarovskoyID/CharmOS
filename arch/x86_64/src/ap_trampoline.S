// Код запуска AP (Application Processor). BSP копирует блоб
// [ap_trampoline_start, ap_trampoline_end) в страницу T ниже 1 МиБ, заполняет
// поля данных и шлёт SIPI с вектором T >> 12. AP начинает в реальном режиме
// с CS = T >> 4, IP = 0 (SDM Vol. 3A §9.4.4.1, табл. 9-1).
//
// Переходы — по SDM §10.8.5: реальный режим → защищённый без страничной
// адресации → PAE, CR3, EFER.LME → CR0.PG → дальний переход в 64-битный код.
// Таблицы страниц трамплина (поле tramp_cr3) лежат ниже 4 ГиБ, содержат
// тождественное отображение первых 2 МиБ (где лежит T) и верхнюю половину ядра.
//
// Код позиционно-независимый: в реальном режиме адресация идёт от DS = CS,
// в 32/64-битном — от EBX/RBX = T.

    .section .rodata.ap_trampoline, "a"
    .globl ap_trampoline_start, ap_trampoline_end
    .globl ap_tramp_gdtr, ap_tramp_gdt, ap_tramp_far32, ap_tramp_far64
    .globl ap_tramp_pm32, ap_tramp_lm64
    .globl ap_tramp_cr3, ap_tramp_stack, ap_tramp_entry, ap_tramp_arg

    .code16
ap_trampoline_start:
    cli
    cld
    mov     %cs, %ax
    mov     %ax, %ds
    xor     %ebx, %ebx
    mov     %cs, %bx
    shl     $4, %ebx                        // EBX = T (линейный адрес страницы)

    lgdtl   ap_tramp_gdtr - ap_trampoline_start
    mov     %cr0, %eax
    or      $1, %eax                        // CR0.PE
    mov     %eax, %cr0
    ljmpl   *(ap_tramp_far32 - ap_trampoline_start)

    .code32
ap_tramp_pm32:
    mov     $0x10, %ax                      // плоский сегмент данных
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %ss

    mov     %cr4, %eax
    or      $((1 << 5) | (1 << 9) | (1 << 10)), %eax   // PAE, OSFXSR, OSXMMEXCPT
    mov     %eax, %cr4
    mov     (ap_tramp_cr3 - ap_trampoline_start)(%ebx), %eax
    mov     %eax, %cr3

    mov     $0xc0000080, %ecx               // IA32_EFER
    rdmsr
    or      $((1 << 8) | (1 << 11)), %eax   // LME, NXE
    wrmsr

    mov     %cr0, %eax
    or      $((1 << 31) | (1 << 16)), %eax  // PG, WP
    mov     %eax, %cr0
    ljmpl   *(ap_tramp_far64 - ap_trampoline_start)(%ebx)

    .code64
ap_tramp_lm64:
    mov     %ebx, %ebx                      // старшая половина RBX не определена
    mov     (ap_tramp_stack - ap_trampoline_start)(%rbx), %rsp
    mov     (ap_tramp_arg - ap_trampoline_start)(%rbx), %rdi
    mov     (ap_tramp_entry - ap_trampoline_start)(%rbx), %rax
    xor     %ebp, %ebp
    pushq   $0                              // фиктивный адрес возврата: RSP+8 кратен 16
    jmp     *%rax

    .align 8
ap_tramp_gdt:
    .quad   0
    .quad   0x00cf9a000000ffff              // 0x08: 32-битный код
    .quad   0x00cf92000000ffff              // 0x10: данные
    .quad   0x00af9a000000ffff              // 0x18: 64-битный код
ap_tramp_gdtr:
    .word   4 * 8 - 1
    .long   0                               // база = T + (ap_tramp_gdt - start), заполняет BSP
ap_tramp_far32:
    .long   0                               // T + (ap_tramp_pm32 - start)
    .word   0x08
ap_tramp_far64:
    .long   0                               // T + (ap_tramp_lm64 - start)
    .word   0x18
    .align 8
ap_tramp_cr3:
    .quad   0
ap_tramp_stack:
    .quad   0
ap_tramp_entry:
    .quad   0
ap_tramp_arg:
    .quad   0
ap_trampoline_end:
