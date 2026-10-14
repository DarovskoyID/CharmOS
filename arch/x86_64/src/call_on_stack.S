// void call_on_stack(void (*fn)(void*), void* arg, uint64_t stack_top)
// Вызывает fn(arg) на другом стеке и возвращается на исходный.
// System V: rdi = fn, rsi = arg, rdx = вершина стека (выровнена на 16).

    .text
    .globl call_on_stack
    .type call_on_stack, @function
call_on_stack:
    push    %rbp
    mov     %rsp, %rbp
    mov     %rdx, %rsp
    mov     %rdi, %rax
    mov     %rsi, %rdi
    call    *%rax
    mov     %rbp, %rsp
    pop     %rbp
    ret
