// Точка входа ядра (docs/abi/boot.md §2): RDI = физ. адрес BootInfo,
// RSP+8 кратен 16. Проверяем выравнивание стека и зовём kernel_main.

    .text
    .globl kernel_entry
    .type kernel_entry, @function
kernel_entry:
    // Второй аргумент kernel_main: 1, если контракт по стеку соблюдён.
    lea     8(%rsp), %rax
    xor     %esi, %esi
    test    $0xf, %al
    sete    %sil
    call    kernel_main
1:  cli
    hlt
    jmp     1b
