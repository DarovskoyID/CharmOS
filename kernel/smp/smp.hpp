#pragma once
// Запуск процессоров и работа на нескольких CPU.

#include <abi/boot.hpp>

#include "cpu.hpp"

namespace kernel::smp {

// До инициализации памяти: BSP получает свою структуру Cpu (GS), GDT/TSS со
// статическими стеками IST и IDT. После этого работают исключения и
// проверка порядка блокировок.
void early_init_bsp();

// После памяти и платформы: стеки IST BSP с защитными страницами, APIC ID.
void late_init_bsp();

// Запускает все процессоры из MADT (по одному) и ждёт их выхода в ядро.
void start_aps();

// Выполняет fn(arg) на всех запущенных CPU (на BSP — в вызывающем потоке),
// возвращается, когда все закончили. Пока нет планировщика — через IPI,
// который будит AP из hlt.
void run_on_all_cpus(void (*fn)(void* arg), void* arg);

}  // namespace kernel::smp
