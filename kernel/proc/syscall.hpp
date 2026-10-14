#pragma once
// Диспетчер системных вызовов (docs/abi/syscalls.md).

#include <stdint.h>

namespace kernel::proc {

// Кадр, который строит syscall_entry (arch/x86_64/src/user.S), по возрастанию адресов.
struct SyscallFrame {
  uint64_t r15, r14, r13, r12, rbp, rbx;
  uint64_t r9, r8, r10, rdx, rsi, rdi, rax;
  uint64_t rip, rflags, rsp;
};
static_assert(sizeof(SyscallFrame) == 16 * 8);

// Настраивает SYSCALL на текущем CPU: IA32_EFER.SCE, STAR, LSTAR, FMASK
// (SDM Vol. 3A §5.8.8, адреса MSR — Vol. 4 табл. 2-2). Вызывать на каждом CPU.
void init_syscall_this_cpu();

}  // namespace kernel::proc
