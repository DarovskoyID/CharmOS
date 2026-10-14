#pragma once
// IDT и диспетчер прерываний (docs/specs/intel-sdm-vol3a.pdf §6.10, §6.14).

#include <stdint.h>

namespace arch::x86_64 {

// Состояние, сохранённое заглушкой isr.S, в порядке возрастания адресов.
struct InterruptFrame {
  uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
  uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
  uint64_t vector;
  uint64_t error_code;  // 0, если процессор его не кладёт
  // Кладёт процессор (SDM рис. 6-9, 64-битный режим):
  uint64_t rip, cs, rflags, rsp, ss;
};
static_assert(sizeof(InterruptFrame) == 22 * 8);

using InterruptHandler = void (*)(InterruptFrame& frame);

// Векторы исключений (SDM табл. 6-1).
inline constexpr uint8_t kVectorDivideError = 0;
inline constexpr uint8_t kVectorNmi = 2;
inline constexpr uint8_t kVectorBreakpoint = 3;
inline constexpr uint8_t kVectorInvalidOpcode = 6;
inline constexpr uint8_t kVectorDoubleFault = 8;
inline constexpr uint8_t kVectorGeneralProtection = 13;
inline constexpr uint8_t kVectorPageFault = 14;
inline constexpr uint8_t kVectorMachineCheck = 18;
inline constexpr uint8_t kFirstExternalVector = 32;

const char* exception_name(uint64_t vector);

// Загружает IDT со всеми 256 векторами.
void init_idt();
// Загружает уже построенную IDT на текущем CPU (для AP).
void load_idt();

// Обработчик конкретного вектора (nullptr — снять).
void set_interrupt_handler(uint8_t vector, InterruptHandler handler);

// Вызывается для векторов без своего обработчика (обычно — паника ядра).
void set_fallback_handler(InterruptHandler handler);

}  // namespace arch::x86_64
