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

#include <abi/boot.hpp>
#include <abi/process.hpp>
#include <abi/syscall.hpp>
#include <lib/cap/handle_table.hpp>
#include <stdint.h>

namespace kernel::sched {
struct Thread;
}

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
// То же под блокировкой IPC: для конца канала, чей другой конец уже у
// процесса (тот может закрыть его одновременно).
void close_detached(Cap* cap);
// Капабилити Device на функцию PCI с индексом pci_index (для запуска драйвера).
Status create_device(uint32_t pci_index, Cap** out);

// Читает сообщение из канала от имени ядра (endpoint — отсоединённая
// капабилити ядра). Хендлы в сообщении закрываются: ядру от драйверов они не
// нужны. *len — длина сообщения (и при BufferTooSmall). deadline — как у
// channel_read.
Status kernel_recv(Cap* endpoint, void* buf, uint64_t cap, uint32_t* len, uint64_t deadline);
// То же, но хендлы сообщения (до ncap) переходят ядру отсоединёнными
// капабилити; *got — сколько. Лишние закрываются.
Status kernel_recv_caps(Cap* endpoint, void* buf, uint64_t cap, uint32_t* len, Cap** caps,
                        uint32_t ncap, uint32_t* got, uint64_t deadline);
// Физические фреймы VMO (не MMIO): до max штук в out, *pages — всего страниц.
// WrongType — не VMO или BAR.
Status vmo_frames(Cap* vmo, uint64_t* out, uint64_t max, uint64_t* pages);

// VMO из pages свежих обнулённых страниц для ядра: *out — отсоединённая
// капабилити (права kVmoDefault без Duplicate), frames[i] — физ. адреса.
Status create_kernel_vmo(uint64_t pages, Cap** out, uint64_t* frames);
// Ещё одна отсоединённая капабилити на тот же объект (с теми же правами).
Status clone_detached(Cap* cap, Cap** out);

// Ссылка ядра на текущий объект Device функции (держит его домен IOMMU).
// nullptr — драйвер ещё не запущен или уже ушёл.
void* acquire_device(uint32_t pci_index, pci::iommu::Domain** domain);
void release_device(void* device);

// --- узлы ФС (docs/abi/fs.md): объект ядра «файл/каталог» по id узла ----------------------
// Хук вызывается, когда объект узла освобождён (VFS ведёт счётчик открытых).
void set_fs_node_closed_hook(void (*hook)(uint64_t node));
Status create_fs_node(uint64_t node, bool dir, uint32_t rights, Cap** out);
Status install_fs_node(uint64_t node, bool dir, uint32_t rights, uint64_t* out);
Status get_fs_node(uint32_t h, uint32_t need, uint64_t* node, bool* dir, uint32_t* rights);

// --- сеть (docs/abi/socket.md) --------------------------------------------------------------
Status create_net(uint32_t rights, Cap** out);
Status get_net(uint32_t h, uint32_t need, uint32_t* rights);
void set_socket_closed_hook(void (*hook)(uint64_t id));
Status install_socket(uint64_t id, uint64_t* out);
Status get_socket(uint32_t h, uint32_t need, uint64_t* id);
// Для стека: пройти подписки на сокеты; ready возвращает true и тип пакета.
void poll_sockets(bool (*ready)(uint64_t id, uint32_t* packet));

// --- запуск процессов и Job (docs/abi/process.md §3, §5) ---------------------------------
// Сообщение в канал (endpoint — отсоединённая капабилити ядра) с хендлами
// из таблицы текущего процесса; атомарно: при ошибке хендлы остаются.
Status send_from_table(Cap* endpoint, const void* bytes, uint64_t len, const uint32_t* handles,
                       uint32_t count);
// Хендл Process в таблице текущего процесса; ссылку на p передаёт вызывающий.
Status install_process(proc::Process* p, uint64_t* handle);
// По хендлу Process: ссылка на процесс (снять — proc::put_process).
Status get_process(uint32_t h, uint32_t need, proc::Process** out);
// Процесс завершился (exit_code задан): пакеты подписчикам.
void process_exited(proc::Process* p);

struct Job;
Job* root_job();
// Предки Job неизменны: проверка без блокировки (оба держатся ссылками).
bool job_contains(const Job* ancestor, const Job* j);
// Процесс входит в Job (с первым потоком) / выходит; лимиты — NoSpace.
Status job_enter(Job* j);
void job_leave(Job* j);
Status job_thread_enter(Job* j);
void job_thread_leave(Job* j);
// По хендлу Job: ссылка (снять — job_put).
Status get_job(uint32_t h, uint32_t need, Job** out);
void job_get(Job* j);
void job_put(Job* j);
Status create_job_cap(Job* j, Cap** out);
Status sys_job_create(uint32_t parent, uint64_t* out);
Status sys_job_info(uint32_t h, uint64_t user_out);
// Передний план консоли (§5.1); with_foreground — под блокировкой IPC (можно из прерывания).
Status set_foreground(Job* j);
void with_foreground(void (*fn)(Job* j, void* ctx), void* ctx);
// v1.6 (pkg.md §2): процесс в переднем Job (или вложенном).
bool in_foreground(const proc::Process* p);

// Лимиты Job (§7): действуют все предки; значения урезаются родителем.
void job_set_limits(Job* j, const abi::process::JobLimits& limits);
Status job_charge_memory(Job* j, uint64_t pages);  // NoMemory — лимит
void job_uncharge_memory(Job* j, uint64_t pages);
// Время CPU: добавляет ns всему пути; возвращает превысивший лимит Job или nullptr.
Job* job_charge_cpu(Job* j, uint64_t ns);
uint32_t job_max_priority(const Job* j);
uint32_t job_handle_limit(const Job* j);  // 0 — без лимита

// --- потоки (docs/abi/process.md §4) ----------------------------------------------------
// Поток объекта Thread (для thread_set_priority); после выхода — отвязан.
void thread_obj_attach(void* obj, sched::Thread* t);
Status thread_set_priority(uint32_t h, uint32_t priority);
// Объект Thread с капабилити в таблице текущего процесса (+ ссылка потока).
Status create_thread_obj(void** obj, uint64_t* handle);
// Поток завершился: пакет подписчику, ссылка потока снимается.
void thread_exited(void* obj, int64_t code);
Status sys_thread_info(uint32_t h, uint64_t user_out);

// --- консоль (docs/abi/console.md) ------------------------------------------------------
Status create_console(uint32_t rights, Cap** out);
Status get_console(uint32_t h, uint32_t need);

// --- питание (docs/abi/power.md) --------------------------------------------------------
Status create_power(uint32_t rights, Cap** out);
Status get_power(uint32_t h, uint32_t need);

// --- экран (docs/abi/display.md) ---------------------------------------------------------
Status create_display(const abi::boot::Framebuffer& fb, uint32_t rights, Cap** out);
Status sys_display_info(uint32_t h, uint64_t user_out);
Status sys_display_map(uint32_t h, uint64_t* out);

// --- ввод (docs/abi/input.md) -------------------------------------------------------------
Status create_input(uint32_t rights, Cap** out);
Status sys_input_attach(uint32_t h, uint32_t vmo);
void input_push(const abi::syscall::InputEvent& e);
Status sys_device_input(uint32_t device, uint64_t event);
void set_console_input_probe(bool (*probe)());
// Из прерывания консоли: пришёл ввод — пакеты подписчикам.
void console_readable();

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
