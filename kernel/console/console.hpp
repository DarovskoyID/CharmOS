#pragma once
// Консоль (docs/abi/console.md): ввод с COM1 по прерыванию IRQ 4 в кольцо
// ядра, вывод — в COM1 через журнал ядра (строки не перемешиваются).
// Ctrl-C и Ctrl-\ — сигналы переднему Job (process.md §5.1).

#include <abi/syscall.hpp>
#include <stdint.h>

namespace kernel::ipc {
struct Cap;
}

namespace kernel::console {

using abi::syscall::Status;

void init();

// Отсоединённая капабилити Console (права Duplicate | Transfer | Read | Write).
Status console_cap(ipc::Cap** out);

// Обработчик управляющих байтов (Ctrl-C → 1, Ctrl-\ → 2): ставит Job-слой.
void set_control_handler(void (*handler)(uint32_t signo));

Status read(void* buf, uint32_t cap, uint64_t deadline, uint32_t* got);
bool readable();
void write(const char* buf, uint64_t len);

Status sys_console_read(uint32_t con, uint64_t buf, uint64_t cap, uint64_t deadline, uint64_t* out);
Status sys_console_write(uint32_t con, uint64_t buf, uint64_t len, uint64_t* out);

}  // namespace kernel::console
