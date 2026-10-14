#pragma once
// Процессы: адресное пространство + один поток в ring 3 (docs/abi/syscalls.md
// §5: в v1.0 у процесса один поток). Создаются из ELF-образа (обычно из initrd).

#include <arch/x86_64/idt.hpp>
#include <lib/core/result.hpp>
#include <lib/core/string_view.hpp>

#include "../mm/address_space.hpp"
#include "../sched/sched.hpp"
#include "../sync/wait.hpp"

namespace kernel::proc {

// Код завершения процесса, убитого ядром (исключение в ring 3).
inline constexpr int64_t kExitKilled = -1000;

// Раскладка нижней половины.
inline constexpr uint64_t kUserImageMin = 0x10000;        // ниже — не отображается
inline constexpr uint64_t kUserStackTop = 0x00007ffffff00000ull;
inline constexpr uint64_t kUserStackSize = 64 * 1024;     // под ним — защитная дыра

enum class ProcError : uint8_t { OutOfMemory, BadImage };

struct Process {
  uint64_t pid;
  char name[32];
  mm::AddressSpace space;
  uint64_t entry;
  uint8_t* fpu;  // начальное, затем сохраняемое состояние FXSAVE потока
  int64_t exit_code;
  sync::Semaphore exited{"proc.exited", 0};
};

lib::core::Result<Process*, ProcError> spawn_process(lib::core::StringView name,
                                                     const void* elf, size_t size,
                                                     int cpu = sched::kAnyCpu);

// Ждёт завершения, освобождает структуру процесса и возвращает код.
int64_t wait_process(Process* p);

Process* current_process();

[[noreturn]] void exit_current(int64_t code);
// Из обработчика исключения, пришедшего из ring 3.
[[noreturn]] void kill_current(const arch::x86_64::InterruptFrame& frame);

}  // namespace kernel::proc
