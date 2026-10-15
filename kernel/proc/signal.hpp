#pragma once
// Сигналы (docs/abi/process.md §9).

#include <abi/syscall.hpp>
#include <arch/x86_64/idt.hpp>
#include <stdint.h>

namespace kernel::proc {

using abi::syscall::Status;
struct Process;
struct SyscallFrame;

// Отправить сигнал процессу (можно из прерывания).
void post_signal(Process* p, uint32_t signo);

// Доставка при возврате в ring 3: кадр меняется на вход в обработчик.
void deliver_signals(SyscallFrame& f);
void deliver_signals(arch::x86_64::InterruptFrame& f);
// Исключение в ring 3: true — уходит в обработчик Fault, иначе — убить.
bool deliver_fault(arch::x86_64::InterruptFrame& f);

// Из тика таймера CPU 0: срабатывание signal_alarm.
void alarm_tick(uint64_t now_ns);

Status sys_signal_send(uint32_t proc, uint64_t signo);
Status sys_signal_set_handler(uint64_t entry, uint64_t stack_top, uint64_t stack_size);
Status sys_signal_mask(uint64_t block, uint64_t unblock, uint64_t* old);
Status sys_signal_alarm(uint64_t deadline);
// Не возвращается, если поток в обработчике (иначе — InvalidArgs вызывающему).
void sys_signal_return();

}  // namespace kernel::proc
