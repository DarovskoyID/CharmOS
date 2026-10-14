#include "panic.hpp"

#include <abi/boot.hpp>
#include <arch/x86_64/cpu.hpp>
#include <arch/x86_64/serial.hpp>

#include "ktest.hpp"

namespace kernel {

using arch::x86_64::InterruptFrame;

void halt_with_failure() {
  arch::x86_64::qemu_exit(arch::x86_64::kExitFailure);
}

void dump_backtrace(uint64_t rbp) {
  kprint("backtrace:\n");
  // Кадр: [rbp] = предыдущий rbp, [rbp+8] = адрес возврата.
  // Идём, пока указатели выглядят как стек ядра в верхней половине.
  for (int depth = 0; depth < 32; ++depth) {
    if (rbp < abi::boot::kHhdmBase || rbp % 8 != 0) break;
    auto* frame = reinterpret_cast<const uint64_t*>(rbp);
    uint64_t ret = frame[1];
    if (ret < abi::boot::kKernelBase) break;
    kprint("  #{} {X}\n", depth, ret);
    uint64_t next = frame[0];
    if (next <= rbp) break;  // стек растёт вниз: цепочка обязана идти вверх
    rbp = next;
  }
}

void dump_frame(const InterruptFrame& f) {
  kprint("EXCEPTION: {} (vector {}) error={x}\n", arch::x86_64::exception_name(f.vector),
         f.vector, f.error_code);
  kprint("  rip={X} cs={x} rflags={X}\n", f.rip, f.cs, f.rflags);
  kprint("  rsp={X} ss={x}\n", f.rsp, f.ss);
  if (f.vector == arch::x86_64::kVectorPageFault) {
    kprint("  cr2={X} ({} {} {})\n", arch::x86_64::read_cr2(),
           (f.error_code & 1) != 0 ? "protection" : "not-present",
           (f.error_code & 2) != 0 ? "write" : "read",
           (f.error_code & 16) != 0 ? "ifetch" : "data");
  }
  kprint("  rax={X} rbx={X} rcx={X}\n", f.rax, f.rbx, f.rcx);
  kprint("  rdx={X} rsi={X} rdi={X}\n", f.rdx, f.rsi, f.rdi);
  kprint("  rbp={X} r8 ={X} r9 ={X}\n", f.rbp, f.r8, f.r9);
  kprint("  r10={X} r11={X} r12={X}\n", f.r10, f.r11, f.r12);
  kprint("  r13={X} r14={X} r15={X}\n", f.r13, f.r14, f.r15);
  kprint("  cr0={X} cr3={X} cr4={X}\n", arch::x86_64::read_cr0(), arch::x86_64::read_cr3(),
         arch::x86_64::read_cr4());
  dump_backtrace(f.rbp);
}

void unhandled_interrupt(InterruptFrame& frame) {
  dump_frame(frame);
  if (ktest::exception_expected(frame)) {
    ktest::pass();
  }
  kprint("KERNEL PANIC: unhandled {}\n", arch::x86_64::exception_name(frame.vector));
  halt_with_failure();
}

}  // namespace kernel
