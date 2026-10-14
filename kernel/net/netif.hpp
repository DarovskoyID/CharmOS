#pragma once
// Сетевой интерфейс ядра (docs/abi/net.md): кадры Ethernet II между стеком
// TCP/IP и драйвером класса net. Один интерфейс (v1.4).

#include <abi/syscall.hpp>
#include <stdint.h>

namespace kernel::ipc {
struct Cap;
}

namespace kernel::net {

using abi::syscall::Status;

// Обслуживает один запуск сетевого драйвера: рукопожатие, приём кадров
// (передаются в input), возврат буферов. Возвращается, когда драйвер ушёл.
void serve(uint32_t pci_index, ipc::Cap* kernel_end, const char* name);

bool link_up();
bool mac(uint8_t out[6]);
uint16_t mtu();

// Отправляет кадр (с заголовком Ethernet). NetUnreachable — драйвера нет или
// нет линии; NoMemory — все буферы заняты (кадр отброшен, как в сети).
Status send_frame(const void* frame, uint32_t len);

// Вызывается для каждого принятого кадра (из потока интерфейса).
void input(const uint8_t* frame, uint32_t len);

}  // namespace kernel::net
