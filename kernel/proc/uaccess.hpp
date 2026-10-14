#pragma once
// Доступ к памяти процесса из ядра. Указатель процесса никогда не
// разыменовывается напрямую: диапазон проверяется на нижнюю половину, а
// копирование идёт через uaccess_copy (user.S), чья ошибка страницы не
// роняет ядро, а превращается в false.

#include <stddef.h>
#include <stdint.h>

#include <arch/x86_64/idt.hpp>

namespace kernel::proc {

bool copy_from_user(void* dst, uint64_t user_src, size_t size);
bool copy_to_user(uint64_t user_dst, const void* src, size_t size);

// Для обработчика #PF ядра: если ошибка — внутри uaccess_copy, переводит
// выполнение на её обработчик и возвращает true.
bool fixup_uaccess_fault(arch::x86_64::InterruptFrame& frame);

}  // namespace kernel::proc
