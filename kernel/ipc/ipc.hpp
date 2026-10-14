#pragma once
// Капабилити, каналы и объекты памяти (docs/abi/ipc.md, syscall ABI v1.1).
//
// Всё состояние IPC (таблицы хендлов процессов, дерево происхождения
// капабилити, очереди каналов, отображения VMO) защищено одной блокировкой
// ранга Ipc. Это сознательно просто: оптимизация — после замера (правило 9).
//
// Сброс TLB при снятии отображений делается после отпускания блокировки, а
// фреймы VMO освобождаются только после сброса — никто не обращается к
// освобождённой памяти через устаревшую запись TLB.

#include <abi/syscall.hpp>
#include <lib/cap/handle_table.hpp>
#include <stdint.h>

namespace kernel::proc {
struct Process;
}
namespace kernel::pci::iommu {
struct Domain;
}

namespace kernel::ipc {

using abi::syscall::Status;

struct Cap;      // капабилити: объект + права + место в дереве происхождения
struct Mapping;  // отображение VMO в адресное пространство процесса

// IPC-часть процесса (встроена в proc::Process).
struct ProcessIpc {
  lib::cap::HandleTable table;
  lib::cap::HandleTable::Slot* slots;
  Mapping* mappings;
  uint64_t map_next;  // следующий свободный адрес для vmo_map
};

// Окно адресов для отображений VMO в процессе.
inline constexpr uint64_t kMapBase = 0x0000100000000000ull;
inline constexpr uint64_t kMapLimit = 0x0000700000000000ull;

Status init_process(proc::Process* p);
// При завершении процесса: CR3 уже не указывает на его пространство.
// Закрывает все хендлы, снимает все отображения VMO (до освобождения
// адресного пространства: его страницы VMO освобождаются отдельно).
void teardown_process(proc::Process* p);

// --- для ядра (стартовые каналы процессов, тесты) --------------------------------
// Капабилити, ещё не лежащие ни в одной таблице («отсоединённые»).
Status create_channel(Cap** end0, Cap** end1);
// Помещает отсоединённую капабилити в таблицу процесса.
Status install(proc::Process* p, Cap* cap, uint32_t* handle);
// Пишет сообщение в канал от имени ядра; caps (отсоединённые) перемещаются.
Status kernel_send(Cap* endpoint, const void* bytes, uint64_t len, Cap* const* caps,
                   uint32_t count);
// Закрывает отсоединённую капабилити, если ядро её не отдало.
void release_detached(Cap* cap);
// Капабилити Device на функцию PCI с индексом pci_index (для запуска драйвера).
Status create_device(uint32_t pci_index, Cap** out);

// Читает сообщение из канала от имени ядра (endpoint — отсоединённая
// капабилити ядра). Хендлы в сообщении закрываются: ядру от драйверов они не
// нужны. *len — длина сообщения (и при BufferTooSmall). deadline — как у
// channel_read.
Status kernel_recv(Cap* endpoint, void* buf, uint64_t cap, uint32_t* len, uint64_t deadline);

// VMO из pages свежих обнулённых страниц для ядра: *out — отсоединённая
// капабилити (права kVmoDefault без Duplicate), frames[i] — физ. адреса.
Status create_kernel_vmo(uint64_t pages, Cap** out, uint64_t* frames);
// Ещё одна отсоединённая капабилити на тот же объект (с теми же правами).
Status clone_detached(Cap* cap, Cap** out);

// Ссылка ядра на текущий объект Device функции (держит его домен IOMMU).
// nullptr — драйвер ещё не запущен или уже ушёл.
void* acquire_device(uint32_t pci_index, pci::iommu::Domain** domain);
void release_device(void* device);

// --- системные вызовы (текущий процесс) -------------------------------------------
Status sys_handle_close(uint32_t h);
Status sys_handle_duplicate(uint32_t h, uint64_t rights, uint64_t* out);
Status sys_handle_revoke(uint32_t h, uint64_t* out);
Status sys_channel_create(uint64_t user_pair);
Status sys_channel_write(uint32_t h, uint64_t bytes, uint64_t len, uint64_t handles,
                         uint64_t count);
Status sys_channel_read(uint32_t h, uint64_t bytes, uint64_t cap, uint64_t handles,
                        uint64_t hcap, uint64_t deadline, uint64_t* out);
Status sys_vmo_create(uint64_t size, uint64_t* out);
Status sys_vmo_map(uint32_t h, uint64_t rights, uint64_t* out);
Status sys_vmo_unmap(uint64_t addr);
Status sys_port_create(uint64_t* out);
Status sys_port_wait(uint32_t port, uint64_t user_packet, uint64_t deadline);
Status sys_object_bind(uint32_t port, uint32_t h, uint64_t key);
Status sys_device_info(uint32_t dev, uint64_t user_out);
Status sys_device_map_bar(uint32_t dev, uint64_t bar, uint64_t* out);
Status sys_device_irq(uint32_t dev, uint64_t index, uint64_t* out);
Status sys_interrupt_ack(uint32_t irq);
Status sys_device_vendor_cap(uint32_t dev, uint64_t index, uint64_t user_buf, uint64_t cap,
                             uint64_t* out);
Status sys_dma_alloc(uint32_t dev, uint64_t size, uint64_t flags, uint64_t user_addr,
                     uint64_t* out);

}  // namespace kernel::ipc
