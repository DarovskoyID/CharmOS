#pragma once
// Сигналы (syscall ABI v1.5, docs/abi/process.md §9): раскладки кадра и
// сведений, которые ядро кладёт на стек сигналов.
//
// Вход в обработчик entry(signo, info, frame): RDI = signo, RSI = &info,
// RDX = &frame; RSP указывает на адрес возврата 0 (как сразу после call —
// функция на C вызывается напрямую, вернуться из неё нельзя: только
// signal_return). Стек: SignalFrame (выровнен на 16), под ним SignalInfo.
// Если поток уже на стеке сигналов (вложенный сигнал), кадр кладётся ниже
// его RSP с отступом 128 байт (красная зона psABI).
//
// На время обработчика маска потока дополняется доставленным сигналом;
// signal_return восстанавливает регистры, FPU и маску из кадра (обработчик
// может их изменить; RIP и RSP — в пользовательской половине, RFLAGS —
// только арифметические флаги и DF, MXCSR — только допустимые биты).

#include <stdint.h>

namespace abi::signal {

struct alignas(16) SignalFrame {
  uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp;
  uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
  uint64_t rip;
  uint64_t rflags;
  uint64_t mask;  // маска потока до обработчика
  uint64_t link;  // для ядра: предыдущий кадр (вложенные сигналы); не менять
  uint8_t fpu[512];  // образ FXSAVE (SDM Vol. 2A, FXSAVE, табл. 3-43)
};
static_assert(sizeof(SignalFrame) == 672);
static_assert(__builtin_offsetof(SignalFrame, rsp) == 56);
static_assert(__builtin_offsetof(SignalFrame, r8) == 64);
static_assert(__builtin_offsetof(SignalFrame, rip) == 128);
static_assert(__builtin_offsetof(SignalFrame, rflags) == 136);
static_assert(__builtin_offsetof(SignalFrame, mask) == 144);
static_assert(__builtin_offsetof(SignalFrame, link) == 152);
static_assert(__builtin_offsetof(SignalFrame, fpu) == 160);

struct SignalInfo {
  uint32_t signo;
  uint32_t code;     // Fault: номер исключения (0 #DE, 6 #UD, 13 #GP, 14 #PF)
  uint64_t addr;     // Fault: для #PF — адрес (CR2), иначе — RIP
  uint64_t error;    // Fault: код ошибки исключения
  uint64_t reserved;
};
static_assert(sizeof(SignalInfo) == 32);
static_assert(__builtin_offsetof(SignalInfo, addr) == 8);
static_assert(__builtin_offsetof(SignalInfo, error) == 16);

// Наименьший стек сигналов: два кадра с запасом.
inline constexpr uint64_t kMinSignalStack = 4096;

}  // namespace abi::signal
