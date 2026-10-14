// Переключение контекста ядра (System V, docs/specs/x86-64-psabi.pdf, рис. 3.4).
// Сохраняются только регистры, которые обязана сохранять вызываемая функция:
// rbx, rbp, r12–r15 (и rsp — в *save_rsp). Остальные вызывающий код уже
// считает испорченными: switch_context — обычный вызов функции. Ядро собрано
// с -mgeneral-regs-only, состояния SSE/x87 у потоков ядра нет.
//
// void switch_context(uint64_t* save_rsp, uint64_t new_rsp)
//   rdi = куда сохранить RSP текущего потока, rsi = RSP потока, на который
//   переключаемся (сохранённый этой же функцией или подготовленный
//   sched::spawn — см. раскладку в thread.cpp).

    .text
    .globl switch_context
    .type switch_context, @function
switch_context:
    pushq   %rbp
    pushq   %rbx
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15
    mov     %rsp, (%rdi)
    mov     %rsi, %rsp
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbx
    popq    %rbp
    ret

// Сюда «возвращается» switch_context при первом запуске потока. Стек выровнен
// так, что здесь RSP кратен 16 — можно сразу вызывать C++.
    .globl thread_start
    .type thread_start, @function
thread_start:
    xor     %ebp, %ebp          // конец цепочки кадров для трассировки
    call    thread_main         // не возвращается
    ud2
