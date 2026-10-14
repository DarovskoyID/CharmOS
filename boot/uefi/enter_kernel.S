// Последний шаг загрузчика: переход в ядро по docs/abi/boot.md §2.
// Вызывается после ExitBootServices, прерывания уже запрещены.
//
// MS x64 ABI (цель UEFI):
//   rcx      — физ. адрес PML4 (новый CR3)
//   rdx      — указатель на дескриптор GDTR (10 байт), база GDT — в HHDM
//   r8       — вершина стека ядра (виртуальный адрес в HHDM, выровнен на 16)
//   r9       — точка входа ядра (e_entry)
//   [rsp+40] — физ. адрес BootInfo → RDI ядра
//
// Код загрузчика продолжает исполняться после смены CR3 только потому, что
// новые таблицы содержат тождественное отображение нижних 4 ГиБ, а образ
// загрузчика лежит ниже 4 ГиБ (проверяется в main.cpp).

    .text
    .globl enter_kernel
enter_kernel:
    cli
    mov     40(%rsp), %rdi
    lgdt    (%rdx)
    mov     %rcx, %cr3
    mov     %r8, %rsp

    // Перезагрузка CS: селектор 0x08 — 64-битный код в GDT загрузчика.
    pushq   $0x08
    lea     1f(%rip), %rax
    pushq   %rax
    lretq
1:
    mov     $0x10, %eax
    mov     %eax, %ds
    mov     %eax, %es
    mov     %eax, %ss
    xor     %eax, %eax
    mov     %eax, %fs
    mov     %eax, %gs

    // Фиктивный адрес возврата: в точке входа RSP+8 кратен 16, как после call.
    pushq   $0
    xor     %ebp, %ebp
    cld
    jmp     *%r9
