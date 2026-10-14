#pragma once
// Процессы: адресное пространство + один поток в ring 3 (docs/abi/syscalls.md
// §5: в v1.0 у процесса один поток). Создаются из ELF-образа (обычно из initrd).

#include <arch/x86_64/idt.hpp>
#include <lib/core/result.hpp>
#include <lib/core/string_view.hpp>

#include "../ipc/ipc.hpp"
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
  ipc::ProcessIpc ipc;
  uint32_t cpu;               // где исполняется поток процесса (не мигрирует)
  uint32_t bootstrap_handle;  // RDI при старте (docs/abi/syscalls.md §7), 0 — нет
  bool kill_requested;        // ядро решило убить процесс (см. request_kill)
};

// bootstrap — отсоединённая капабилити стартового канала (или nullptr);
// при успехе переходит процессу, при ошибке остаётся у вызывающего.
lib::core::Result<Process*, ProcError> spawn_process(lib::core::StringView name,
                                                     const void* elf, size_t size,
                                                     int cpu = sched::kAnyCpu,
                                                     ipc::Cap* bootstrap = nullptr);

// Ждёт завершения, освобождает структуру процесса и возвращает код.
// before_free(ctx) вызывается после завершения, но до освобождения.
int64_t wait_process(Process* p, void (*before_free)(void*) = nullptr, void* ctx = nullptr);

// Просит убить процесс (код kExitKilled). Можно из прерывания. Поток процесса
// завершится при ближайшем выходе из системного вызова или по тику таймера в
// ring 3; поток, заблокированный без срока, — когда проснётся.
void request_kill(Process* p);
// Из обработчиков, вернувшихся бы в ring 3: завершает текущий процесс, если
// его просили убить.
void check_kill();

Process* current_process();

[[noreturn]] void exit_current(int64_t code);
// Из обработчика исключения, пришедшего из ring 3.
[[noreturn]] void kill_current(const arch::x86_64::InterruptFrame& frame);

}  // namespace kernel::proc
