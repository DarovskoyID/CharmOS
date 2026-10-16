#include "ipc.hpp"

#include <arch/x86_64/cpu.hpp>
#include <abi/process.hpp>
#include <arch/x86_64/paging.hpp>

#include "../acpi/acpi.hpp"
#include "../core/panic.hpp"
#include "../mm/heap.hpp"
#include "../mm/layout.hpp"
#include "../mm/pmm.hpp"
#include "../pci/iommu.hpp"
#include "../pci/pci.hpp"
#include "../platform/platform.hpp"
#include "../proc/process.hpp"
#include "../proc/uaccess.hpp"
#include "../sched/sched.hpp"
#include "../smp/cpu.hpp"
#include "../smp/smp.hpp"
#include "../sync/spinlock.hpp"
#include "../sync/wait.hpp"

namespace kernel::ipc {

namespace rights = abi::syscall::rights;
using abi::syscall::kChannelMaxBytes;
using abi::syscall::kChannelMaxHandles;
using abi::syscall::kChannelMaxQueue;
using arch::x86_64::kPageSize;

// --- объекты ------------------------------------------------------------------------

enum class Type : uint8_t { Endpoint, Vmo, Port, Device, Interrupt, FsNode, Net, Socket, Console, Thread, Process, Job, Power, Display, Input, Audio };

struct Object {
  Type type;
  uint32_t refs;  // капабилити + (для VMO) отображения
};

struct Message {
  Message* next;
  uint32_t bytes;
  uint32_t ncaps;
  Cap* caps[kChannelMaxHandles];  // nullptr — отозвана в пути
  uint8_t data[];
};

struct Port;

struct Endpoint {
  Object base;  // первым: Object* ↔ Endpoint*
  Endpoint* peer;  // nullptr — другой конец закрыт
  Message* head;
  Message* tail;
  uint32_t count;
  sync::WaitList readers;
  Port* bound;  // однократная подписка (держит ссылку на порт)
  uint64_t bound_key;
};

struct Port {
  Object base;
  abi::syscall::PortPacket packets[abi::syscall::kPortMaxPackets];
  uint32_t head;
  uint32_t count;  // в очереди
  uint32_t armed;  // активных подписок: count + armed ≤ kPortMaxPackets
  sync::WaitList waiters;
};

struct Vmo {
  Object base;
  uint64_t size;
  uint64_t pages;
  uint64_t* frames;
  bool mmio;  // BAR устройства: некэшируемо, фреймы не принадлежат VMO (kMmioHole — не отображать)
  bool wc;    // при mmio: write-combining (фреймбуфер, display.md §2.2)
  struct Device* dma_dev;  // DMA-буфер: устройство (держит ссылку), в чьём домене он виден
  uint64_t iova;           // адрес буфера для устройства
  struct Job* charged;     // Job, которому учтены страницы (держит ссылку), или nullptr
};

struct Interrupt;

constexpr uint64_t kMmioHole = ~uint64_t{0};

// Векторов на устройство (MSI-X бывает до 2048; больше драйверам пока не нужно).
constexpr uint32_t kMaxDeviceIrqs = 32;

struct Device {
  Object base;
  uint32_t pci_index;
  Interrupt* irqs[kMaxDeviceIrqs];  // не больше одного живого объекта на индекс
  pci::iommu::Domain* domain;  // nullptr — IOMMU нет или функцию он не обслуживает
};

struct Interrupt {
  Object base;
  Device* dev;     // держит ссылку
  uint32_t index;  // индекс device_irq (запись таблицы MSI-X)
  bool msix;
  uint8_t vector;
  bool masked;     // после доставки — до interrupt_ack
  bool hw_mask;    // функция умеет маскировать вектор сама
  bool intx;       // линия INTx (driver.md §10), общая с другими устройствами
  bool owed;       // INTx: срабатывание доставлено, ждём interrupt_ack
  uint64_t pending;
  uint64_t last_ns;
  Port* bound;
  uint64_t bound_key;
};

// Файл или каталог (docs/abi/fs.md): узел смонтированной ФС.
struct FsNode {
  Object base;
  uint64_t node;
  bool dir;
};

constinit void (*g_fs_node_closed)(uint64_t node) = nullptr;

// Сеть (docs/abi/socket.md): Net — право, Socket — соединение стека.
struct NetObj {
  Object base;
};
struct SocketObj {
  Object base;
  uint64_t id;  // идентификатор сокета в kernel/net/socket
  Port* bound;  // однократная подписка на готовность (опрос стеком)
  uint64_t bound_key;
};
constinit void (*g_socket_closed)(uint64_t id) = nullptr;

// Консоль (docs/abi/console.md): объект на капабилити; подписка — однократная.
struct ConsoleObj {
  Object base;
  Port* bound;
  uint64_t bound_key;
};
constexpr uint32_t kMaxConsoleBinds = 8;
constinit bool (*console_has_input)() = nullptr;
constinit ConsoleObj* g_console_binds[kMaxConsoleBinds] = {};

// Ввод (docs/abi/input.md): объектов может быть несколько (дубликаты), кольцо и
// подписка — общие на систему (одно кольцо: у компоновщика).
struct InputObj {
  Object base;
};
constinit Vmo* g_input_ring = nullptr;  // держит ссылку
constinit Port* g_input_port = nullptr;  // одноразовая подписка (как у консоли)
constinit uint64_t g_input_key = 0;

// Запущенный процесс (docs/abi/process.md §3): держит ссылку на proc::Process.
struct ProcessObj {
  Object base;
  proc::Process* proc;
  ProcessObj* next_watch;  // в списке proc->watchers, пока процесс жив
  Port* bound;
  uint64_t bound_key;
};

// Job (docs/abi/process.md §5). Ссылки: капабилити, процессы, дочерние Job,
// передний план консоли. Счётчики — по всему поддереву.
struct Job {
  Object base;
  Job* parent;  // не меняется: предки читаются без блокировки
  uint32_t processes;
  uint32_t threads;
  uint64_t memory_pages;
  uint64_t cpu_ns;
  abi::process::JobLimits limits;  // действующие (не больше, чем у родителя)
  Port* bound;
  uint64_t bound_key;
};
constinit Job* g_root_job = nullptr;

void uncharge_locked(Job* j, uint64_t pages) {
  for (Job* a = j; a != nullptr; a = a->parent) a->memory_pages -= pages;
}
constinit Job* g_foreground = nullptr;  // передний Job консоли (§5.1)

// Поток процесса (docs/abi/process.md §4). Ссылки: капабилити + сам поток,
// пока не завершился.
struct ThreadObj {
  Object base;
  sched::Thread* thread;  // пока поток жив
  bool exited;
  int64_t code;
  Port* bound;
  uint64_t bound_key;
};
constexpr uint32_t kMaxSocketBinds = 64;
constinit SocketObj* g_socket_binds[kMaxSocketBinds] = {};

enum class Where : uint8_t { Detached, Table, Message, Closed };

struct Cap {
  Object* obj;
  uint32_t rights;
  Where where;
  proc::Process* owner;  // Where::Table
  uint32_t handle;       // Where::Table
  Message* msg;          // Where::Message
  Cap* parent;
  Cap* first_child;
  Cap* next_sibling;
  Cap* prev_sibling;
  Mapping* mappings;  // отображения, сделанные через эту капабилити
};

struct Mapping {
  proc::Process* proc;
  uint64_t addr;
  uint64_t size;
  Vmo* vmo;
  Cap* via;
  Mapping* next_in_proc;
  Mapping* next_in_cap;
};

namespace {

constinit sync::Spinlock g_lock{sync::LockRank::Ipc, "ipc"};
// Последний созданный объект Device для функции PCI (под g_lock).
constinit Device* g_current_device[pci::kMaxFunctions] = {};
constinit Cap* g_audio_door = nullptr;

// Снятые отображения, чьи страницы ещё могут быть в TLB других CPU. Объекты
// освобождаются после smp::flush_tlb для всех CPU из cpus.
struct Pending {
  Mapping* list = nullptr;
  uint64_t cpus = 0;  // битовая маска (smp::kMaxCpus ≤ 64)
};

template <typename T>
T* alloc_zeroed(size_t size) {
  auto r = mm::kmalloc(size);
  if (r.is_err()) return nullptr;
  __builtin_memset(r.value(), 0, size);
  return static_cast<T*>(r.value());
}

proc::Process* me() { return proc::current_process(); }

// --- дерево происхождения ---------------------------------------------------------------

void link_child(Cap* parent, Cap* c) {
  c->parent = parent;
  c->prev_sibling = nullptr;
  c->next_sibling = parent != nullptr ? parent->first_child : nullptr;
  if (parent != nullptr) {
    if (parent->first_child != nullptr) parent->first_child->prev_sibling = c;
    parent->first_child = c;
  }
}

void unlink(Cap* c) {
  if (c->prev_sibling != nullptr) {
    c->prev_sibling->next_sibling = c->next_sibling;
  } else if (c->parent != nullptr) {
    c->parent->first_child = c->next_sibling;
  }
  if (c->next_sibling != nullptr) c->next_sibling->prev_sibling = c->prev_sibling;
  // Дети переходят к родителю: закрытие — не отзыв, и предок должен
  // по-прежнему мочь их отозвать.
  Cap* child = c->first_child;
  while (child != nullptr) {
    Cap* next = child->next_sibling;
    link_child(c->parent, child);
    child = next;
  }
  c->first_child = nullptr;
  c->parent = c->next_sibling = c->prev_sibling = nullptr;
}

Cap* new_cap(Object* obj, uint32_t r, Cap* parent) {
  auto* c = alloc_zeroed<Cap>(sizeof(Cap));
  if (c == nullptr) return nullptr;
  c->obj = obj;
  c->rights = r;
  c->where = Where::Detached;
  ++obj->refs;
  link_child(parent, c);
  return c;
}

void release_object(Object* obj);

void port_push(Port* port, uint64_t key, uint32_t type, uint64_t d0 = 0, uint64_t d1 = 0) {
  uint32_t tail = (port->head + port->count) % abi::syscall::kPortMaxPackets;
  port->packets[tail] = {key, type, 0, d0, d1};
  ++port->count;
  if (sched::Thread* t = port->waiters.pop()) sched::wake(t);
}

// Сработала подписка на конце канала: пакет в порт, подписка снята.
void fire(Endpoint* ep, uint32_t type) {
  Port* port = ep->bound;
  if (port == nullptr) return;
  ep->bound = nullptr;
  --port->armed;
  port_push(port, ep->bound_key, type);
  release_object(&port->base);
}

constinit Interrupt* g_vector_irq[256] = {};

// Линии INTx (driver.md §10): один вектор на GSI, пользователи делят линию.
// Вывод I/O APIC замаскирован от срабатывания до подтверждения всеми.
struct IntxLine {
  uint32_t gsi;
  uint8_t vector;
  arch::x86_64::IoApic::Route route;
  Interrupt* users[8];
  uint32_t count;
};
constexpr uint32_t kMaxIntxLines = 16;
constinit IntxLine g_intx[kMaxIntxLines] = {};

IntxLine* intx_line_by_vector(uint8_t v) {
  for (IntxLine& l : g_intx) {
    if (l.count != 0 && l.vector == v) return &l;
  }
  return nullptr;
}

IntxLine* intx_line_of(const Interrupt* irq) {
  for (IntxLine& l : g_intx) {
    for (uint32_t i = 0; i < l.count; ++i) {
      if (l.users[i] == irq) return &l;
    }
  }
  return nullptr;
}

void intx_unmask_if_done(IntxLine* l) {
  for (uint32_t i = 0; i < l->count; ++i) {
    if (l->users[i]->owed) return;
  }
  if (const auto* io = platform::io_apic_for(l->gsi)) io->route(l->gsi, l->route);
}

// Маскирование вектора в устройстве, если оно это умеет.
void hw_mask(Interrupt* irq, bool masked) {
  const pci::Function& f = pci::function(irq->dev->pci_index);
  if (irq->msix) {
    pci::msix_set_mask(f, irq->index, masked);
  } else if (irq->hw_mask) {
    pci::msi_set_mask(f, masked);
  }
}

// Доставка срабатываний в порт: пакет, сброс счётчика, маска до ack.
void deliver(Interrupt* irq) {
  Port* port = irq->bound;
  irq->bound = nullptr;
  --port->armed;
  port_push(port, irq->bound_key, abi::syscall::kPacketInterrupt, irq->pending, irq->last_ns);
  irq->pending = 0;
  irq->masked = true;
  hw_mask(irq, true);
  release_object(&port->base);
}

void intx_interrupt(arch::x86_64::InterruptFrame& frame) {
  {
    sync::SpinGuard guard(g_lock);
    IntxLine* l = intx_line_by_vector(static_cast<uint8_t>(frame.vector & 0xff));
    if (l != nullptr) {
      // По уровню: держим вывод замаскированным, пока все не подтвердят.
      if (const auto* io = platform::io_apic_for(l->gsi)) io->mask(l->gsi);
      for (uint32_t i = 0; i < l->count; ++i) {
        Interrupt* irq = l->users[i];
        ++irq->pending;
        irq->last_ns = platform::now_ns();
        irq->owed = true;
        if (!irq->masked && irq->bound != nullptr) deliver(irq);
      }
    }
  }
  platform::local_apic().eoi();
}

void device_interrupt(arch::x86_64::InterruptFrame& frame) {
  {
    sync::SpinGuard guard(g_lock);
    Interrupt* irq = g_vector_irq[frame.vector & 0xff];
    if (irq != nullptr) {
      ++irq->pending;
      irq->last_ns = platform::now_ns();
      if (!irq->masked && irq->bound != nullptr) deliver(irq);
    }
  }
  platform::local_apic().eoi();
}

void destroy_message(Message* m) {
  for (uint32_t i = 0; i < m->ncaps; ++i) {
    Cap* c = m->caps[i];
    if (c == nullptr) continue;
    if (c->mappings != nullptr) {
      // Через неё уже отобразили VMO (до пересылки): живёт до снятия отображений.
      c->where = Where::Closed;
      c->msg = nullptr;
      continue;
    }
    unlink(c);
    release_object(c->obj);
    mm::kfree(c);
  }
  mm::kfree(m);
}

void release_object(Object* obj) {
  if (--obj->refs != 0) return;
  if (obj->type == Type::Vmo) {
    auto* v = reinterpret_cast<Vmo*>(obj);
    // DMA-буфер: сначала устройство теряет к нему доступ (с инвалидацией IOTLB).
    if (v->dma_dev != nullptr && v->dma_dev->domain != nullptr && v->iova != 0) {
      pci::iommu::unmap(v->dma_dev->domain, v->iova, v->pages);
    }
    for (uint64_t i = 0; i < v->pages && !v->mmio; ++i) mm::free_frames(v->frames[i]);
    mm::kfree(v->frames);
    Device* dev = v->dma_dev;
    Job* charged = v->charged;
    uint64_t pages = v->pages;
    mm::kfree(v);
    if (dev != nullptr) release_object(&dev->base);
    if (charged != nullptr) {
      uncharge_locked(charged, pages);
      release_object(&charged->base);
    }
    return;
  }
  if (obj->type == Type::Port) {
    mm::kfree(obj);  // подписки держат ссылку: сюда попадаем, когда их нет
    return;
  }
  if (obj->type == Type::Net) {
    mm::kfree(obj);
    return;
  }
  if (obj->type == Type::Process) {
    auto* po = reinterpret_cast<ProcessObj*>(obj);
    for (auto** it = reinterpret_cast<ProcessObj**>(&po->proc->watchers); *it != nullptr; it = &(*it)->next_watch) {
      if (*it == po) {
        *it = po->next_watch;
        break;
      }
    }
    if (po->bound != nullptr) {
      --po->bound->armed;
      release_object(&po->bound->base);
    }
    proc::put_process(po->proc);
    mm::kfree(obj);
    return;
  }
  if (obj->type == Type::Job) {
    auto* j = reinterpret_cast<Job*>(obj);
    if (j->bound != nullptr) {
      --j->bound->armed;
      release_object(&j->bound->base);
    }
    Job* parent = j->parent;
    mm::kfree(obj);
    if (parent != nullptr) release_object(&parent->base);
    return;
  }
  if (obj->type == Type::Thread) {
    auto* to = reinterpret_cast<ThreadObj*>(obj);
    if (to->bound != nullptr) {
      --to->bound->armed;
      release_object(&to->bound->base);
    }
    mm::kfree(obj);
    return;
  }
  if (obj->type == Type::Console) {
    auto* co = reinterpret_cast<ConsoleObj*>(obj);
    for (ConsoleObj*& b : g_console_binds) {
      if (b == co) b = nullptr;
    }
    if (co->bound != nullptr) {
      --co->bound->armed;
      release_object(&co->bound->base);
    }
    mm::kfree(obj);
    return;
  }
  if (obj->type == Type::Socket) {
    auto* so = reinterpret_cast<SocketObj*>(obj);
    for (SocketObj*& b : g_socket_binds) {
      if (b == so) b = nullptr;
    }
    if (so->bound != nullptr) {
      --so->bound->armed;
      release_object(&so->bound->base);
    }
    uint64_t id = so->id;
    mm::kfree(obj);
    if (g_socket_closed != nullptr) g_socket_closed(id);
    return;
  }
  if (obj->type == Type::FsNode) {
    uint64_t node = reinterpret_cast<FsNode*>(obj)->node;
    mm::kfree(obj);
    if (g_fs_node_closed != nullptr) g_fs_node_closed(node);
    return;
  }
  if (obj->type == Type::Interrupt && reinterpret_cast<Interrupt*>(obj)->intx) {
    auto* irq = reinterpret_cast<Interrupt*>(obj);
    if (IntxLine* l = intx_line_of(irq)) {
      for (uint32_t i = 0; i < l->count; ++i) {
        if (l->users[i] == irq) {
          l->users[i] = l->users[--l->count];
          break;
        }
      }
      if (l->count == 0) {
        if (const auto* io = platform::io_apic_for(l->gsi)) io->mask(l->gsi);
        platform::free_device_vector(l->vector);
      } else {
        intx_unmask_if_done(l);
      }
    }
    if (irq->bound != nullptr) {
      --irq->bound->armed;
      release_object(&irq->bound->base);
    }
    irq->dev->irqs[irq->index] = nullptr;
    release_object(&irq->dev->base);
    mm::kfree(irq);
    return;
  }
  if (obj->type == Type::Interrupt) {
    auto* irq = reinterpret_cast<Interrupt*>(obj);
    if (irq->msix) {
      pci::msix_set_mask(pci::function(irq->dev->pci_index), irq->index, true);
    } else {
      pci::msi_disable(pci::function(irq->dev->pci_index));
    }
    g_vector_irq[irq->vector] = nullptr;
    platform::free_device_vector(irq->vector);
    if (irq->bound != nullptr) {
      --irq->bound->armed;
      release_object(&irq->bound->base);
    }
    irq->dev->irqs[irq->index] = nullptr;
    release_object(&irq->dev->base);
    mm::kfree(irq);
    return;
  }
  if (obj->type == Type::Device) {
    auto* d = reinterpret_cast<Device*>(obj);
    // Драйвер ушёл: устройство перестаёт обращаться к памяти и шине — если
    // функцией уже не управляет следующий запуск драйвера.
    if (g_current_device[d->pci_index] == d) {
      pci::set_command_bits(pci::function(d->pci_index), 0, pci::kCommandBusMaster);
      g_current_device[d->pci_index] = nullptr;
    }
    if (d->domain != nullptr) pci::iommu::domain_destroy(d->domain);
    mm::kfree(obj);
    return;
  }
  if (obj->type == Type::Power || obj->type == Type::Display || obj->type == Type::Input || obj->type == Type::Audio) {
    mm::kfree(obj);
    return;
  }
  auto* ep = reinterpret_cast<Endpoint*>(obj);
  if (ep->bound != nullptr) {
    Port* port = ep->bound;
    ep->bound = nullptr;
    --port->armed;
    release_object(&port->base);
  }
  if (ep->peer != nullptr) {
    Endpoint* peer = ep->peer;
    peer->peer = nullptr;
    // Читатели другого конца получат PeerClosed, когда очередь опустеет.
    while (sched::Thread* t = peer->readers.pop()) sched::wake(t);
    fire(peer, abi::syscall::kPacketPeerClosed);
  }
  while (Message* m = ep->head) {
    ep->head = m->next;
    destroy_message(m);
  }
  mm::kfree(ep);
}

// Снять отображение под блокировкой: страницы убраны из таблиц процесса,
// объект и структура уходят в pending до сброса TLB.
void detach_mapping(Mapping* m, Pending* p) {
  for (Mapping** it = &m->proc->ipc.mappings; *it != nullptr; it = &(*it)->next_in_proc) {
    if (*it == m) {
      *it = m->next_in_proc;
      break;
    }
  }
  for (Mapping** it = &m->via->mappings; *it != nullptr; it = &(*it)->next_in_cap) {
    if (*it == m) {
      *it = m->next_in_cap;
      break;
    }
  }
  for (uint64_t off = 0; off < m->size; off += kPageSize) m->proc->space.unmap(m->addr + off);
  p->cpus |= __atomic_load_n(&m->proc->cpus, __ATOMIC_ACQUIRE);
  m->next_in_proc = p->list;
  p->list = m;
}

// Капабилити в состоянии Closed живёт только ради своих отображений.
void maybe_free_closed(Cap* c) {
  if (c->where == Where::Closed && c->mappings == nullptr) {
    unlink(c);
    release_object(c->obj);
    mm::kfree(c);
  }
}

void finish_pending(Pending& p) {
  for (uint32_t cpu = 0; cpu < smp::kMaxCpus; ++cpu) {
    if ((p.cpus & (1ull << cpu)) != 0) smp::flush_tlb(cpu);
  }
  sync::SpinGuard guard(g_lock);
  while (Mapping* m = p.list) {
    p.list = m->next_in_proc;
    release_object(&m->vmo->base);
    mm::kfree(m);
  }
}

Cap* lookup(proc::Process* p, uint32_t h) {
  return static_cast<Cap*>(p->ipc.table.lookup(h));
}

// Закрытие хендла (не отзыв): отображения остаются, пока их не снимут.
void close_cap(Cap* c) {
  c->where = Where::Closed;
  c->owner = nullptr;
  maybe_free_closed(c);
}

}  // namespace

// --- процесс ----------------------------------------------------------------------------

Status init_process(proc::Process* p) {
  // Ёмкость таблицы — лимит хендлов Job на момент запуска (process.md §7).
  uint32_t limit = job_handle_limit(p->job);
  uint32_t kSlots = limit != 0 && limit < abi::syscall::kMaxHandles ? limit + 1 : abi::syscall::kMaxHandles;
  auto* slots = alloc_zeroed<lib::cap::HandleTable::Slot>(sizeof(lib::cap::HandleTable::Slot) * kSlots);
  if (slots == nullptr) return Status::NoMemory;
  p->ipc.slots = slots;
  p->ipc.table.init(slots, kSlots);
  p->ipc.mappings = nullptr;
  p->ipc.map_next = kMapBase;
  return Status::Ok;
}

void teardown_process(proc::Process* p) {
  bool irq = g_lock.lock();
  uint32_t cursor = 0;
  while (uint32_t h = p->ipc.table.next_used(&cursor)) {
    close_cap(static_cast<Cap*>(p->ipc.table.remove(h)));
  }
  // CR3 этого процесса больше нигде не загружен (каждый его поток, уходя,
  // перешёл на таблицы ядра, что сбросило TLB) — сброс TLB не нужен.
  while (Mapping* m = p->ipc.mappings) {
    Pending pend;
    detach_mapping(m, &pend);
    Cap* via = m->via;
    release_object(&m->vmo->base);
    mm::kfree(m);
    maybe_free_closed(via);
  }
  g_lock.unlock(irq);
  if (p->ipc.slots != nullptr) mm::kfree(p->ipc.slots);
  p->ipc.slots = nullptr;
}

// --- ядро -------------------------------------------------------------------------------

Status create_channel(Cap** end0, Cap** end1) {
  auto* a = alloc_zeroed<Endpoint>(sizeof(Endpoint));
  auto* b = alloc_zeroed<Endpoint>(sizeof(Endpoint));
  if (a == nullptr || b == nullptr) {
    if (a != nullptr) mm::kfree(a);
    if (b != nullptr) mm::kfree(b);
    return Status::NoMemory;
  }
  a->base.type = b->base.type = Type::Endpoint;
  a->peer = b;
  b->peer = a;
  sync::SpinGuard guard(g_lock);
  Cap* ca = new_cap(&a->base, rights::kChannelDefault, nullptr);
  Cap* cb = new_cap(&b->base, rights::kChannelDefault, nullptr);
  if (ca == nullptr || cb == nullptr) {
    if (ca != nullptr) release_detached(ca);
    if (cb != nullptr) release_detached(cb);
    // Концы без капабилити: освобождаем вручную.
    if (ca == nullptr) mm::kfree(a);
    if (cb == nullptr) mm::kfree(b);
    return Status::NoMemory;
  }
  *end0 = ca;
  *end1 = cb;
  return Status::Ok;
}

Status install(proc::Process* p, Cap* cap, uint32_t* handle) {
  sync::SpinGuard guard(g_lock);
  auto h = p->ipc.table.insert(cap);
  if (h.is_err()) return Status::NoMemory;
  cap->where = Where::Table;
  cap->owner = p;
  cap->handle = h.value();
  *handle = h.value();
  return Status::Ok;
}

void close_detached(Cap* cap) {
  sync::SpinGuard guard(g_lock);
  release_detached(cap);
}

void release_detached(Cap* cap) {
  // Может вызываться и под g_lock (из create_channel), поэтому без блокировки:
  // отсоединённая капабилити никому больше не видна.
  unlink(cap);
  release_object(cap->obj);
  mm::kfree(cap);
}

Status kernel_send(Cap* endpoint, const void* bytes, uint64_t len, Cap* const* caps,
                   uint32_t count) {
  if (len > kChannelMaxBytes || count > kChannelMaxHandles) return Status::InvalidArgs;
  auto* m = alloc_zeroed<Message>(sizeof(Message) + len);
  if (m == nullptr) return Status::NoMemory;
  __builtin_memcpy(m->data, bytes, len);
  m->bytes = static_cast<uint32_t>(len);
  sync::SpinGuard guard(g_lock);
  auto* ep = reinterpret_cast<Endpoint*>(endpoint->obj);
  if (ep->peer == nullptr) {
    mm::kfree(m);
    return Status::PeerClosed;
  }
  for (uint32_t i = 0; i < count; ++i) {
    caps[i]->where = Where::Message;
    caps[i]->msg = m;
    m->caps[i] = caps[i];
  }
  m->ncaps = count;
  Endpoint* peer = ep->peer;
  if (peer->tail != nullptr) {
    peer->tail->next = m;
  } else {
    peer->head = m;
  }
  peer->tail = m;
  ++peer->count;
  if (sched::Thread* t = peer->readers.pop()) sched::wake(t);
  fire(peer, abi::syscall::kPacketChannelReadable);
  return Status::Ok;
}

// --- хендлы -----------------------------------------------------------------------------

Status sys_handle_close(uint32_t h) {
  sync::SpinGuard guard(g_lock);
  auto* c = static_cast<Cap*>(me()->ipc.table.remove(h));
  if (c == nullptr) return Status::BadHandle;
  close_cap(c);
  return Status::Ok;
}

Status sys_handle_duplicate(uint32_t h, uint64_t new_rights, uint64_t* out) {
  if ((new_rights & ~uint64_t{rights::kAll | rights::kNetAll | rights::kSignal | rights::kSpawn}) != 0) {
    return Status::InvalidArgs;
  }
  sync::SpinGuard guard(g_lock);
  proc::Process* p = me();
  Cap* c = lookup(p, h);
  if (c == nullptr) return Status::BadHandle;
  if ((c->rights & rights::kDuplicate) == 0) return Status::AccessDenied;
  if ((new_rights & ~uint64_t{c->rights}) != 0) return Status::AccessDenied;  // только сужение
  Cap* d = new_cap(c->obj, static_cast<uint32_t>(new_rights), c);
  if (d == nullptr) return Status::NoMemory;
  auto nh = p->ipc.table.insert(d);
  if (nh.is_err()) {
    release_detached(d);
    return Status::NoMemory;
  }
  d->where = Where::Table;
  d->owner = p;
  d->handle = nh.value();
  *out = nh.value();
  return Status::Ok;
}

Status sys_handle_revoke(uint32_t h, uint64_t* out) {
  Pending pend;
  uint64_t destroyed = 0;
  {
    sync::SpinGuard guard(g_lock);
    Cap* root = lookup(me(), h);
    if (root == nullptr) return Status::BadHandle;
    // Обход потомков снизу вверх: сначала уничтожаются листья.
    while (root->first_child != nullptr) {
      Cap* c = root->first_child;
      while (c->first_child != nullptr) c = c->first_child;
      while (Mapping* m = c->mappings) detach_mapping(m, &pend);
      switch (c->where) {
        case Where::Table:
          c->owner->ipc.table.remove(c->handle);
          ++destroyed;
          break;
        case Where::Message:
          for (uint32_t i = 0; i < c->msg->ncaps; ++i) {
            if (c->msg->caps[i] == c) c->msg->caps[i] = nullptr;
          }
          ++destroyed;
          break;
        case Where::Detached:
        case Where::Closed:
          break;
      }
      unlink(c);
      // Объект освобождается позже (отображения ещё держат VMO, а сам
      // release_object может освободить канал с сообщениями — это безопасно
      // под блокировкой).
      release_object(c->obj);
      mm::kfree(c);
    }
  }
  finish_pending(pend);
  *out = destroyed;
  return Status::Ok;
}

// --- каналы -----------------------------------------------------------------------------

Status sys_channel_create(uint64_t user_pair) {
  Cap* a;
  Cap* b;
  Status s = create_channel(&a, &b);
  if (s != Status::Ok) return s;
  proc::Process* p = me();
  uint32_t ha = 0, hb = 0;
  if (install(p, a, &ha) != Status::Ok) {
    release_detached(a);
    release_detached(b);
    return Status::NoMemory;
  }
  if (install(p, b, &hb) != Status::Ok) {
    (void)sys_handle_close(ha);
    release_detached(b);
    return Status::NoMemory;
  }
  uint32_t pair[2] = {ha, hb};
  if (!proc::copy_to_user(user_pair, pair, sizeof pair)) {
    (void)sys_handle_close(ha);
    (void)sys_handle_close(hb);
    return Status::BadAddress;
  }
  return Status::Ok;
}

Status sys_channel_write(uint32_t h, uint64_t bytes, uint64_t len, uint64_t handles,
                         uint64_t count) {
  if (len > kChannelMaxBytes || count > kChannelMaxHandles) return Status::InvalidArgs;
  uint32_t values[kChannelMaxHandles];
  if (!proc::copy_from_user(values, handles, count * sizeof(uint32_t))) return Status::BadAddress;
  auto* m = alloc_zeroed<Message>(sizeof(Message) + len);
  if (m == nullptr) return Status::NoMemory;
  if (!proc::copy_from_user(m->data, bytes, len)) {
    mm::kfree(m);
    return Status::BadAddress;
  }
  m->bytes = static_cast<uint32_t>(len);

  Status result = Status::Ok;
  {
    sync::SpinGuard guard(g_lock);
    proc::Process* p = me();
    Cap* c = lookup(p, h);
    Cap* moved[kChannelMaxHandles] = {};
    if (c == nullptr) {
      result = Status::BadHandle;
    } else if (c->obj->type != Type::Endpoint) {
      result = Status::WrongType;
    } else if ((c->rights & rights::kWrite) == 0) {
      result = Status::AccessDenied;
    } else if (reinterpret_cast<Endpoint*>(c->obj)->peer == nullptr) {
      result = Status::PeerClosed;
    } else if (reinterpret_cast<Endpoint*>(c->obj)->peer->count >= kChannelMaxQueue) {
      result = Status::WouldBlock;
    } else {
      // Проверяем всё до того, как что-то переместить: сообщение атомарно.
      for (uint64_t i = 0; i < count && result == Status::Ok; ++i) {
        Cap* t = lookup(p, values[i]);
        if (t == nullptr) {
          result = Status::BadHandle;
        } else if (t == c) {
          result = Status::InvalidArgs;  // свой конец в себя не отправить
        } else if ((t->rights & rights::kTransfer) == 0) {
          result = Status::AccessDenied;
        } else {
          for (uint64_t j = 0; j < i; ++j) {
            if (moved[j] == t) result = Status::InvalidArgs;  // повтор хендла
          }
          moved[i] = t;
        }
      }
    }
    if (result == Status::Ok) {
      for (uint64_t i = 0; i < count; ++i) {
        p->ipc.table.remove(moved[i]->handle);
        moved[i]->where = Where::Message;
        moved[i]->owner = nullptr;
        moved[i]->msg = m;
        m->caps[i] = moved[i];
      }
      m->ncaps = static_cast<uint32_t>(count);
      Endpoint* peer = reinterpret_cast<Endpoint*>(c->obj)->peer;
      if (peer->tail != nullptr) {
        peer->tail->next = m;
      } else {
        peer->head = m;
      }
      peer->tail = m;
      ++peer->count;
      if (sched::Thread* t = peer->readers.pop()) sched::wake(t);
      fire(peer, abi::syscall::kPacketChannelReadable);
      m = nullptr;
    }
  }
  if (m != nullptr) mm::kfree(m);
  return result;
}

Status sys_channel_read(uint32_t h, uint64_t bytes, uint64_t cap, uint64_t handles,
                        uint64_t hcap, uint64_t deadline, uint64_t* out) {
  for (;;) {
    bool irq = g_lock.lock();
    proc::Process* p = me();
    Cap* c = lookup(p, h);
    Status s = Status::Ok;
    if (c == nullptr) {
      s = Status::BadHandle;
    } else if (c->obj->type != Type::Endpoint) {
      s = Status::WrongType;
    } else if ((c->rights & rights::kRead) == 0) {
      s = Status::AccessDenied;
    }
    if (s != Status::Ok) {
      g_lock.unlock(irq);
      return s;
    }
    auto* ep = reinterpret_cast<Endpoint*>(c->obj);
    if (Message* m = ep->head) {
      uint32_t n = 0;
      for (uint32_t i = 0; i < m->ncaps; ++i) n += m->caps[i] != nullptr ? 1 : 0;
      *out = (uint64_t{m->bytes} << 32) | n;
      if (m->bytes > cap || n > hcap) {
        g_lock.unlock(irq);
        return Status::BufferTooSmall;
      }
      if (p->ipc.table.capacity() - p->ipc.table.used() < n) {
        g_lock.unlock(irq);
        return Status::NoMemory;
      }
      if (!proc::copy_to_user(bytes, m->data, m->bytes)) {
        g_lock.unlock(irq);
        return Status::BadAddress;
      }
      uint32_t values[kChannelMaxHandles];
      uint32_t k = 0;
      for (uint32_t i = 0; i < m->ncaps; ++i) {
        Cap* t = m->caps[i];
        if (t == nullptr) continue;
        values[k++] = p->ipc.table.insert(t).value();  // место проверено выше
      }
      if (!proc::copy_to_user(handles, values, k * sizeof(uint32_t))) {
        for (uint32_t i = 0; i < k; ++i) p->ipc.table.remove(values[i]);
        g_lock.unlock(irq);
        return Status::BadAddress;
      }
      k = 0;
      for (uint32_t i = 0; i < m->ncaps; ++i) {
        Cap* t = m->caps[i];
        if (t == nullptr) continue;
        t->where = Where::Table;
        t->owner = p;
        t->handle = values[k++];
        t->msg = nullptr;
      }
      ep->head = m->next;
      if (ep->head == nullptr) ep->tail = nullptr;
      --ep->count;
      mm::kfree(m);
      g_lock.unlock(irq);
      return Status::Ok;
    }
    if (ep->peer == nullptr) {
      g_lock.unlock(irq);
      return Status::PeerClosed;
    }
    if (deadline == 0) {
      g_lock.unlock(irq);
      return Status::WouldBlock;
    }
    if (deadline != ~uint64_t{0} && platform::now_ns() >= deadline) {
      g_lock.unlock(irq);
      return Status::TimedOut;
    }
    if (sched::interrupted()) {
      g_lock.unlock(irq);
      return Status::Interrupted;
    }
    sched::Thread* self = sched::current();
    ep->readers.push(self);
    sched::block_interruptible_until(deadline);
    g_lock.unlock(false);
    sched::schedule();
    sched::end_interruptible();
    // Проснулись: либо писатель вынул нас из очереди, либо истёк срок
    // (тогда мы ещё в очереди). Конец канала мог исчезнуть — ищем заново.
    {
      sync::SpinGuard guard(g_lock);
      Cap* again = lookup(p, h);
      if (again != nullptr && again->obj == &ep->base) ep->readers.remove(self);
    }
    if (irq) arch::x86_64::sti();
  }
}

// --- объекты памяти ---------------------------------------------------------------------

Status sys_vmo_create(uint64_t size, uint64_t* out) {
  if (size == 0 || size % kPageSize != 0 || size > abi::syscall::kVmoMaxSize) {
    return Status::InvalidArgs;
  }
  uint64_t pages = size / kPageSize;
  Job* job = me()->job;
  if (job_charge_memory(job, pages) != Status::Ok) return Status::NoMemory;  // лимит Job (§7)
  auto* v = alloc_zeroed<Vmo>(sizeof(Vmo));
  auto* frames = alloc_zeroed<uint64_t>(pages * sizeof(uint64_t));
  if (v == nullptr || frames == nullptr) {
    if (v != nullptr) mm::kfree(v);
    if (frames != nullptr) mm::kfree(frames);
    job_uncharge_memory(job, pages);
    return Status::NoMemory;
  }
  for (uint64_t i = 0; i < pages; ++i) {
    auto f = mm::alloc_frames();
    if (f.is_err()) {
      for (uint64_t k = 0; k < i; ++k) mm::free_frames(frames[k]);
      mm::kfree(frames);
      mm::kfree(v);
      job_uncharge_memory(job, pages);
      return Status::NoMemory;
    }
    __builtin_memset(mm::phys_to_virt<void>(f.value()), 0, kPageSize);
    frames[i] = f.value();
  }
  v->base.type = Type::Vmo;
  v->size = size;
  v->pages = pages;
  v->frames = frames;

  sync::SpinGuard guard(g_lock);
  v->charged = job;
  ++job->base.refs;
  Cap* c = new_cap(&v->base, rights::kVmoDefault, nullptr);
  if (c == nullptr) {
    v->base.refs = 1;
    release_object(&v->base);
    return Status::NoMemory;
  }
  proc::Process* p = me();
  auto h = p->ipc.table.insert(c);
  if (h.is_err()) {
    release_detached(c);
    return Status::NoMemory;
  }
  c->where = Where::Table;
  c->owner = p;
  c->handle = h.value();
  *out = h.value();
  return Status::Ok;
}

Status sys_vmo_map(uint32_t h, uint64_t req, uint64_t* out) {
  if ((req & ~uint64_t{rights::kRead | rights::kWrite}) != 0 || (req & rights::kRead) == 0) {
    return Status::InvalidArgs;  // исполнения нет; запись без чтения x86 не умеет
  }
  sync::SpinGuard guard(g_lock);
  proc::Process* p = me();
  Cap* c = lookup(p, h);
  if (c == nullptr) return Status::BadHandle;
  if (c->obj->type != Type::Vmo) return Status::WrongType;
  if ((c->rights & rights::kMap) == 0 || (req & ~uint64_t{c->rights}) != 0) {
    return Status::AccessDenied;
  }
  auto* v = reinterpret_cast<Vmo*>(c->obj);
  if (v->size > kMapLimit - p->ipc.map_next) return Status::NoMemory;
  auto* m = alloc_zeroed<Mapping>(sizeof(Mapping));
  if (m == nullptr) return Status::NoMemory;
  uint64_t addr = p->ipc.map_next;
  uint64_t flags = arch::x86_64::kMapNoExec | ((req & rights::kWrite) != 0 ? arch::x86_64::kMapWrite : 0);
  if (v->mmio) flags |= v->wc ? arch::x86_64::kMapWriteCombine : arch::x86_64::kMapCacheDisable;
  for (uint64_t i = 0; i < v->pages; ++i) {
    if (v->frames[i] == kMmioHole) continue;
    if (p->space.map_phys(addr + i * kPageSize, v->frames[i], flags).is_err()) {
      for (uint64_t k = 0; k < i; ++k) p->space.unmap(addr + k * kPageSize);
      mm::kfree(m);
      return Status::NoMemory;  // страницы ещё не были видны — сброс TLB не нужен
    }
  }
  p->ipc.map_next += v->size + kPageSize;  // защитная дыра между отображениями
  m->proc = p;
  m->addr = addr;
  m->size = v->size;
  m->vmo = v;
  m->via = c;
  ++v->base.refs;
  m->next_in_proc = p->ipc.mappings;
  p->ipc.mappings = m;
  m->next_in_cap = c->mappings;
  c->mappings = m;
  *out = addr;
  return Status::Ok;
}

Status sys_vmo_unmap(uint64_t addr) {
  Pending pend;
  {
    sync::SpinGuard guard(g_lock);
    Mapping* m = me()->ipc.mappings;
    while (m != nullptr && m->addr != addr) m = m->next_in_proc;
    if (m == nullptr) return Status::InvalidArgs;
    Cap* via = m->via;
    detach_mapping(m, &pend);
    maybe_free_closed(via);
  }
  finish_pending(pend);
  return Status::Ok;
}

// --- порты ------------------------------------------------------------------------------

Status sys_port_create(uint64_t* out) {
  auto* port = alloc_zeroed<Port>(sizeof(Port));
  if (port == nullptr) return Status::NoMemory;
  port->base.type = Type::Port;
  sync::SpinGuard guard(g_lock);
  Cap* c = new_cap(&port->base, rights::kPortDefault, nullptr);
  if (c == nullptr) {
    mm::kfree(port);
    return Status::NoMemory;
  }
  proc::Process* p = me();
  auto h = p->ipc.table.insert(c);
  if (h.is_err()) {
    release_detached(c);
    return Status::NoMemory;
  }
  c->where = Where::Table;
  c->owner = p;
  c->handle = h.value();
  *out = h.value();
  return Status::Ok;
}

Status sys_object_bind(uint32_t port_h, uint32_t h, uint64_t key) {
  sync::SpinGuard guard(g_lock);
  proc::Process* p = me();
  Cap* pc = lookup(p, port_h);
  Cap* oc = lookup(p, h);
  if (pc == nullptr || oc == nullptr) return Status::BadHandle;
  if (pc->obj->type != Type::Port) return Status::WrongType;
  if (oc->obj->type == Type::Interrupt) {
    if ((pc->rights & rights::kWrite) == 0 || (oc->rights & rights::kRead) == 0) {
      return Status::AccessDenied;
    }
    auto* port = reinterpret_cast<Port*>(pc->obj);
    auto* irq = reinterpret_cast<Interrupt*>(oc->obj);
    if (irq->bound != nullptr) return Status::Busy;
    if (port->count + port->armed >= abi::syscall::kPortMaxPackets) return Status::Busy;
    irq->bound = port;
    irq->bound_key = key;
    ++port->armed;
    ++port->base.refs;
    if (!irq->masked && irq->pending != 0) deliver(irq);
    return Status::Ok;
  }
  if (oc->obj->type == Type::Process || oc->obj->type == Type::Job) {
    Port** bound;
    uint64_t* bound_key;
    bool fire_now;
    uint32_t packet;
    uint64_t data0 = 0;
    if (oc->obj->type == Type::Process) {
      auto* po = reinterpret_cast<ProcessObj*>(oc->obj);
      if ((oc->rights & rights::kRead) == 0) return Status::AccessDenied;
      bound = &po->bound;
      bound_key = &po->bound_key;
      fire_now = __atomic_load_n(&po->proc->dead, __ATOMIC_ACQUIRE);
      packet = abi::syscall::kPacketProcessExited;
      data0 = static_cast<uint64_t>(po->proc->exit_code);
    } else {
      auto* j = reinterpret_cast<Job*>(oc->obj);
      bound = &j->bound;
      bound_key = &j->bound_key;
      fire_now = j->processes == 0;
      packet = abi::syscall::kPacketJobEmpty;
    }
    if (*bound != nullptr) return Status::Busy;
    auto* port = reinterpret_cast<Port*>(pc->obj);
    if (port->count + port->armed >= abi::syscall::kPortMaxPackets) return Status::Busy;
    if (fire_now) {
      port_push(port, key, packet, data0);
      return Status::Ok;
    }
    *bound = port;
    *bound_key = key;
    ++port->armed;
    ++port->base.refs;
    return Status::Ok;
  }
  if (oc->obj->type == Type::Thread) {
    auto* to = reinterpret_cast<ThreadObj*>(oc->obj);
    if (to->bound != nullptr) return Status::Busy;
    auto* port = reinterpret_cast<Port*>(pc->obj);
    if (port->count + port->armed >= abi::syscall::kPortMaxPackets) return Status::Busy;
    if (to->exited) {
      port_push(port, key, abi::syscall::kPacketThreadExited, static_cast<uint64_t>(to->code));
      return Status::Ok;
    }
    to->bound = port;
    to->bound_key = key;
    ++port->armed;
    ++port->base.refs;
    return Status::Ok;
  }
  if (oc->obj->type == Type::Input) {
    if ((oc->rights & rights::kRead) == 0) return Status::AccessDenied;
    if (g_input_port != nullptr) return Status::Busy;
    auto* port = reinterpret_cast<Port*>(pc->obj);
    if (port->count + port->armed >= abi::syscall::kPortMaxPackets) return Status::Busy;
    if (g_input_ring != nullptr) {
      auto* rh = mm::phys_to_virt<abi::syscall::InputRingHeader>(g_input_ring->frames[0]);
      if (__atomic_load_n(&rh->tail, __ATOMIC_ACQUIRE) != __atomic_load_n(&rh->head, __ATOMIC_ACQUIRE)) {
        port_push(port, key, abi::syscall::kPacketInputReadable);  // события уже есть
        return Status::Ok;
      }
    }
    g_input_port = port;
    g_input_key = key;
    ++port->armed;
    ++port->base.refs;
    return Status::Ok;
  }
  if (oc->obj->type == Type::Console) {
    auto* co = reinterpret_cast<ConsoleObj*>(oc->obj);
    // v1.6 (pkg.md §2): только передний Job. g_lock уже взят — проверка на месте.
    proc::Process* self = me();
    if (g_foreground == nullptr || self == nullptr || !job_contains(g_foreground, self->job)) {
      return Status::AccessDenied;
    }
    if (co->bound != nullptr) return Status::Busy;
    auto* port = reinterpret_cast<Port*>(pc->obj);
    if (port->count + port->armed >= abi::syscall::kPortMaxPackets) return Status::Busy;
    ConsoleObj** slot = nullptr;
    for (ConsoleObj*& b : g_console_binds) {
      if (b == nullptr) {
        slot = &b;
        break;
      }
    }
    if (slot == nullptr) return Status::NoMemory;
    co->bound = port;
    co->bound_key = key;
    ++port->armed;
    ++port->base.refs;
    *slot = co;
    if (console_has_input != nullptr && console_has_input()) {
      // Ввод уже есть: пакет сразу.
      *slot = nullptr;
      co->bound = nullptr;
      --port->armed;
      port_push(port, key, abi::syscall::kPacketConsoleReadable);
      release_object(&port->base);
    }
    return Status::Ok;
  }
  if (oc->obj->type == Type::Socket) {
    auto* so = reinterpret_cast<SocketObj*>(oc->obj);
    if (so->bound != nullptr) return Status::Busy;
    auto* port = reinterpret_cast<Port*>(pc->obj);
    if (port->count + port->armed >= abi::syscall::kPortMaxPackets) return Status::Busy;
    SocketObj** slot = nullptr;
    for (SocketObj*& b : g_socket_binds) {
      if (b == nullptr) {
        slot = &b;
        break;
      }
    }
    if (slot == nullptr) return Status::NoMemory;
    so->bound = port;
    so->bound_key = key;
    ++port->armed;
    ++port->base.refs;
    *slot = so;  // стек опрашивает готовность (poll_sockets) и шлёт пакет
    return Status::Ok;
  }
  if (oc->obj->type != Type::Endpoint) return Status::WrongType;
  if ((pc->rights & rights::kWrite) == 0 || (oc->rights & rights::kRead) == 0) {
    return Status::AccessDenied;
  }
  auto* port = reinterpret_cast<Port*>(pc->obj);
  auto* ep = reinterpret_cast<Endpoint*>(oc->obj);
  if (ep->bound != nullptr) return Status::Busy;
  if (port->count + port->armed >= abi::syscall::kPortMaxPackets) return Status::Busy;
  // Событие уже наступило — пакет сразу.
  if (ep->head != nullptr) {
    port_push(port, key, abi::syscall::kPacketChannelReadable);
    return Status::Ok;
  }
  if (ep->peer == nullptr) {
    port_push(port, key, abi::syscall::kPacketPeerClosed);
    return Status::Ok;
  }
  ep->bound = port;
  ep->bound_key = key;
  ++port->armed;
  ++port->base.refs;
  return Status::Ok;
}

Status sys_port_wait(uint32_t port_h, uint64_t user_packet, uint64_t deadline) {
  for (;;) {
    bool irq = g_lock.lock();
    proc::Process* p = me();
    Cap* c = lookup(p, port_h);
    Status s = Status::Ok;
    if (c == nullptr) {
      s = Status::BadHandle;
    } else if (c->obj->type != Type::Port) {
      s = Status::WrongType;
    } else if ((c->rights & rights::kRead) == 0) {
      s = Status::AccessDenied;
    }
    if (s != Status::Ok) {
      g_lock.unlock(irq);
      return s;
    }
    auto* port = reinterpret_cast<Port*>(c->obj);
    if (port->count != 0) {
      if (!proc::copy_to_user(user_packet, &port->packets[port->head],
                              sizeof(abi::syscall::PortPacket))) {
        g_lock.unlock(irq);
        return Status::BadAddress;
      }
      port->head = (port->head + 1) % abi::syscall::kPortMaxPackets;
      --port->count;
      g_lock.unlock(irq);
      return Status::Ok;
    }
    if (deadline == 0) {
      g_lock.unlock(irq);
      return Status::WouldBlock;
    }
    if (deadline != ~uint64_t{0} && platform::now_ns() >= deadline) {
      g_lock.unlock(irq);
      return Status::TimedOut;
    }
    if (sched::interrupted()) {
      g_lock.unlock(irq);
      return Status::Interrupted;
    }
    sched::Thread* self = sched::current();
    port->waiters.push(self);
    sched::block_interruptible_until(deadline);
    g_lock.unlock(false);
    sched::schedule();
    sched::end_interruptible();
    {
      sync::SpinGuard guard(g_lock);
      Cap* again = lookup(p, port_h);
      if (again != nullptr && again->obj == &port->base) port->waiters.remove(self);
    }
    if (irq) arch::x86_64::sti();
  }
}

// --- устройства -------------------------------------------------------------------------

Status create_device(uint32_t pci_index, Cap** out) {
  auto* d = alloc_zeroed<Device>(sizeof(Device));
  if (d == nullptr) return Status::NoMemory;
  d->base.type = Type::Device;
  d->pci_index = pci_index;
  // Декодирование памяти включено; Bus Master — только вместе с MSI или DMA.
  pci::set_command_bits(pci::function(pci_index), pci::kCommandMemory, pci::kCommandBusMaster);
  // Свой домен IOMMU, пока пустой: устройство не видит никакой памяти
  // (docs/abi/driver.md §5). Прежний запуск драйвера теряет функцию.
  if (pci::iommu::active()) {
    d->domain = pci::iommu::domain_create();
    if (d->domain == nullptr) {
      mm::kfree(d);
      return Status::NoMemory;
    }
    if (!pci::iommu::attach(d->domain, pci_index)) {
      pci::iommu::domain_destroy(d->domain);
      d->domain = nullptr;
    }
  }
  sync::SpinGuard guard(g_lock);
  Cap* c = new_cap(&d->base, rights::kDeviceDefault, nullptr);
  if (c == nullptr) {
    if (d->domain != nullptr) pci::iommu::domain_destroy(d->domain);
    mm::kfree(d);
    return Status::NoMemory;
  }
  g_current_device[pci_index] = d;
  *out = c;
  return Status::Ok;
}

namespace {

Device* device_from(uint32_t h, uint32_t need, Status* s) {
  Cap* c = lookup(me(), h);
  if (c == nullptr) {
    *s = Status::BadHandle;
  } else if (c->obj->type != Type::Device) {
    *s = Status::WrongType;
  } else if ((c->rights & need) != need) {
    *s = Status::AccessDenied;
  } else {
    *s = Status::Ok;
    return reinterpret_cast<Device*>(c->obj);
  }
  return nullptr;
}

Status install_new(Object* obj, uint32_t r, uint64_t* out) {
  Cap* c = new_cap(obj, r, nullptr);
  if (c == nullptr) return Status::NoMemory;
  proc::Process* p = me();
  auto h = p->ipc.table.insert(c);
  if (h.is_err()) {
    release_detached(c);
    return Status::NoMemory;
  }
  c->where = Where::Table;
  c->owner = p;
  c->handle = h.value();
  *out = h.value();
  return Status::Ok;
}

}  // namespace

namespace {

// MSI-X, если есть (до kMaxDeviceIrqs векторов), иначе один вектор MSI,
// иначе линия INTx по _PRT (driver.md §10).
uint32_t irq_count(uint32_t pci_index) {
  const pci::Function& f = pci::function(pci_index);
  uint32_t n = pci::msix_vectors(f);
  if (n != 0) return n < kMaxDeviceIrqs ? n : kMaxDeviceIrqs;
  if (f.msi_cap != 0) return 1;
  acpi::Intx line;
  return acpi::intx(pci_index, &line) ? 1 : 0;
}

// DMA выдаётся только в домен IOMMU, без него — лишь при явном iommu=off.
bool dma_available(const Device* d) {
  return d->domain != nullptr || pci::iommu::disabled_by_cmdline();
}

}  // namespace

Status create_audio(uint32_t r, Cap** out) {
  auto* object = alloc_zeroed<Object>(sizeof(Object));
  if (object == nullptr) return Status::NoMemory;
  object->type = Type::Audio;
  sync::SpinGuard guard(g_lock);
  Cap* c = new_cap(object, r, nullptr);
  if (c == nullptr) { mm::kfree(object); return Status::NoMemory; }
  *out = c; return Status::Ok;
}
Status register_audio(Cap* door) {
  sync::SpinGuard guard(g_lock);
  if (door == nullptr || door->obj->type != Type::Endpoint) return Status::WrongType;
  constexpr uint32_t need = rights::kWrite | rights::kDuplicate | rights::kTransfer;
  if ((door->rights & need) != need) return Status::AccessDenied;
  if (g_audio_door != nullptr) return Status::Busy;
  g_audio_door = door; return Status::Ok;
}
void unregister_audio(Cap* door) {
  sync::SpinGuard guard(g_lock);
  if (g_audio_door == door) g_audio_door = nullptr;
}
Status sys_audio_open(uint32_t h, uint64_t* out) {
  sync::SpinGuard guard(g_lock);
  proc::Process* p = me(); Cap* token = lookup(p, h);
  if (token == nullptr) return Status::BadHandle;
  if (token->obj->type != Type::Audio) return Status::WrongType;
  if ((token->rights & rights::kWrite) == 0) return Status::AccessDenied;
  if (g_audio_door == nullptr) return Status::NotFound;
  Cap* c = new_cap(g_audio_door->obj, rights::kWrite | rights::kDuplicate | rights::kTransfer, g_audio_door);
  if (c == nullptr) return Status::NoMemory;
  auto handle = p->ipc.table.insert(c);
  if (handle.is_err()) { release_detached(c); return Status::NoMemory; }
  c->where = Where::Table; c->owner = p; c->handle = handle.value(); *out = handle.value();
  return Status::Ok;
}

Status sys_device_input(uint32_t h, uint64_t user_event) {
  {
    sync::SpinGuard guard(g_lock);
    Status s;
    Device* d = device_from(h, rights::kWrite, &s);
    if (d == nullptr) return s;
    if (pci::function(d->pci_index).class_code != 0x0c0330) return Status::AccessDenied;
  }
  abi::syscall::InputEvent e{};
  if (!proc::copy_from_user(&e, user_event, sizeof e)) return Status::BadAddress;
  bool valid = false;
  if (e.type == abi::syscall::kInputKey) valid = e.code >= 4 && e.code <= 0xe7 && e.value >= 0 && e.value <= 2 && e.x == 0 && e.y == 0;
  if (e.type == abi::syscall::kInputMotion) valid = e.code == 0 && e.value == 0 && e.x >= -32768 && e.x <= 32767 && e.y >= -32768 && e.y <= 32767;
  if (e.type == abi::syscall::kInputButton) valid = e.code >= 1 && e.code <= 3 && (e.value == 0 || e.value == 1) && e.x == 0 && e.y == 0;
  if (e.type == abi::syscall::kInputWheel) valid = e.code == 0 && e.value >= -127 && e.value <= 127 && e.x == 0 && e.y == 0;
  if (!valid) return Status::InvalidArgs;
  e.time_ns = platform::now_ns();
  input_push(e);
  return Status::Ok;
}

Status sys_device_info(uint32_t h, uint64_t user_out) {
  abi::syscall::DeviceInfo info = {};
  {
    sync::SpinGuard guard(g_lock);
    Status s;
    Device* d = device_from(h, rights::kRead, &s);
    if (d == nullptr) return s;
    const pci::Function& f = pci::function(d->pci_index);
    info.segment = f.segment;
    info.bus = f.bus;
    info.device = f.device;
    info.function = f.function;
    info.revision = f.revision;
    info.vendor = f.vendor;
    info.device_id = f.device_id;
    info.class_code = f.class_code;
    info.irq_count = irq_count(d->pci_index);
    info.flags = dma_available(d) ? abi::syscall::kDeviceDma : 0;
    for (int i = 0; i < 6; ++i) {
      switch (f.bars[i].kind) {
        case pci::BarKind::Mem32: info.bar_kind[i] = abi::syscall::kBarMem32; break;
        case pci::BarKind::Mem64: info.bar_kind[i] = abi::syscall::kBarMem64; break;
        case pci::BarKind::Io: info.bar_kind[i] = abi::syscall::kBarIo; break;
        case pci::BarKind::Absent: info.bar_kind[i] = abi::syscall::kBarAbsent; break;
      }
      bool mem = f.bars[i].kind == pci::BarKind::Mem32 || f.bars[i].kind == pci::BarKind::Mem64;
      info.bar_size[i] = mem ? f.bars[i].size : 0;
    }
  }
  if (!proc::copy_to_user(user_out, &info, sizeof info)) return Status::BadAddress;
  return Status::Ok;
}

Status sys_device_map_bar(uint32_t h, uint64_t bar, uint64_t* out) {
  if (bar >= 6) return Status::InvalidArgs;
  uint64_t base, size;
  uint32_t pci_index;
  {
    sync::SpinGuard guard(g_lock);
    Status s;
    Device* d = device_from(h, rights::kMap, &s);
    if (d == nullptr) return s;
    pci_index = d->pci_index;
    const pci::Bar& b = pci::function(d->pci_index).bars[bar];
    if (b.kind == pci::BarKind::Io) return Status::NotSupported;
    if (b.kind == pci::BarKind::Absent) return Status::InvalidArgs;
    base = b.base;
    size = b.size;
  }
  // BAR меньше страницы отображается страницей целиком: размер BAR степень
  // двойки и выровнен по себе, но соседние BAR могут оказаться в той же
  // странице (pci-reference.md §3) — такие устройства пока не поддерживаем.
  if (base % kPageSize != 0) return Status::NotSupported;
  uint64_t pages = (size + kPageSize - 1) / kPageSize;
  auto* v = alloc_zeroed<Vmo>(sizeof(Vmo));
  auto* frames = alloc_zeroed<uint64_t>(pages * sizeof(uint64_t));
  if (v == nullptr || frames == nullptr) {
    if (v != nullptr) mm::kfree(v);
    if (frames != nullptr) mm::kfree(frames);
    return Status::NoMemory;
  }
  for (uint64_t i = 0; i < pages; ++i) frames[i] = base + i * kPageSize;
  // Таблица MSI-X задаёт адрес и данные сообщений: драйвер, меняющий её,
  // посылал бы любые прерывания. Её страницы (и PBA) в отображении — дыры.
  uint64_t first, last;
  if (pci::msix_bar_pages(pci::function(pci_index), static_cast<uint32_t>(bar), &first, &last)) {
    for (uint64_t i = first; i <= last && i < pages; ++i) frames[i] = kMmioHole;
  }
  v->base.type = Type::Vmo;
  v->size = pages * kPageSize;
  v->pages = pages;
  v->frames = frames;
  v->mmio = true;
  sync::SpinGuard guard(g_lock);
  Status s = install_new(&v->base, rights::kMmioDefault, out);
  if (s != Status::Ok) {
    mm::kfree(frames);
    mm::kfree(v);
  }
  return s;
}

// Под g_lock: хендл Interrupt на линию INTx (общую — с одним вектором).
Status device_intx(Device* d, const acpi::Intx& line, uint64_t* out) {
  const auto* io = platform::io_apic_for(line.gsi);
  if (io == nullptr) return Status::NotSupported;
  IntxLine* l = nullptr;
  for (IntxLine& x : g_intx) {
    if (x.count != 0 && x.gsi == line.gsi) l = &x;
  }
  bool fresh = l == nullptr;
  if (fresh) {
    for (IntxLine& x : g_intx) {
      if (x.count == 0) {
        l = &x;
        break;
      }
    }
    if (l == nullptr) return Status::NoMemory;
  }
  if (l->count == 8) return Status::NoMemory;
  auto* irq = alloc_zeroed<Interrupt>(sizeof(Interrupt));
  if (irq == nullptr) return Status::NoMemory;
  if (fresh) {
    uint8_t vector = platform::alloc_device_vector(&intx_interrupt);
    if (vector == 0) {
      mm::kfree(irq);
      return Status::NoMemory;
    }
    *l = {};
    l->gsi = line.gsi;
    l->vector = vector;
    l->route = {vector, line.level, line.active_low, static_cast<uint8_t>(smp::cpu(0).apic_id)};
  }
  irq->base.type = Type::Interrupt;
  irq->dev = d;
  ++d->base.refs;
  irq->vector = l->vector;
  irq->intx = true;
  d->irqs[0] = irq;
  l->users[l->count++] = irq;
  Status s = install_new(&irq->base, rights::kInterruptDefault, out);
  if (s != Status::Ok) return s;  // объект уже освобождён (и снят с линии)
  pci::set_command_bits(pci::function(d->pci_index), 0, pci::kCommandIntxDisable);
  if (fresh) io->route(line.gsi, l->route);
  return Status::Ok;
}

Status sys_device_irq(uint32_t h, uint64_t index, uint64_t* out) {
  sync::SpinGuard guard(g_lock);
  Status s;
  Device* d = device_from(h, rights::kRead, &s);
  if (d == nullptr) return s;
  const pci::Function& f = pci::function(d->pci_index);
  if (index >= irq_count(d->pci_index)) return Status::OutOfRange;
  if (d->irqs[index] != nullptr) return Status::Busy;
  acpi::Intx line{};
  if (pci::msix_vectors(f) == 0 && f.msi_cap == 0 && acpi::intx(d->pci_index, &line)) {
    return device_intx(d, line, out);
  }
  bool msix = pci::msix_vectors(f) != 0;
  if (msix && !pci::msix_prepare(d->pci_index)) return Status::NoMemory;
  auto* irq = alloc_zeroed<Interrupt>(sizeof(Interrupt));
  if (irq == nullptr) return Status::NoMemory;
  uint8_t vector = platform::alloc_device_vector(&device_interrupt);
  if (vector == 0) {
    mm::kfree(irq);
    return Status::NoMemory;
  }
  irq->base.type = Type::Interrupt;
  irq->dev = d;
  ++d->base.refs;
  irq->vector = vector;
  irq->index = static_cast<uint32_t>(index);
  irq->msix = msix;
  d->irqs[index] = irq;
  s = install_new(&irq->base, rights::kInterruptDefault, out);
  if (s != Status::Ok) {
    // install_new уже освободил капабилити и объект (refs → 0).
    return s;
  }
  g_vector_irq[vector] = irq;
  if (msix) {
    irq->hw_mask = true;
    pci::msix_program(f, irq->index, smp::cpu(0).apic_id, vector);
    pci::msix_enable(f);
    pci::msix_set_mask(f, irq->index, false);
  } else {
    irq->hw_mask = pci::msi_set_mask(f, false);
    pci::msi_enable(f, smp::cpu(0).apic_id, vector);
  }
  // Сообщение MSI — запись устройства в память, без Bus Master его нет. С
  // IOMMU устройство при этом видит только свой домен (MSI идут мимо
  // трансляции DMA). Без IOMMU изоляции DMA нет вовсе (DRIVERS.md §2.1).
  pci::set_command_bits(f, pci::kCommandBusMaster, 0);
  return Status::Ok;
}

Status sys_interrupt_ack(uint32_t h) {
  sync::SpinGuard guard(g_lock);
  Cap* c = lookup(me(), h);
  if (c == nullptr) return Status::BadHandle;
  if (c->obj->type != Type::Interrupt) return Status::WrongType;
  if ((c->rights & rights::kWrite) == 0) return Status::AccessDenied;
  auto* irq = reinterpret_cast<Interrupt*>(c->obj);
  irq->masked = false;
  if (irq->intx) {
    irq->owed = false;
    if (IntxLine* l = intx_line_of(irq)) intx_unmask_if_done(l);
  }
  hw_mask(irq, false);
  if (irq->pending != 0 && irq->bound != nullptr) deliver(irq);
  return Status::Ok;
}

Status sys_dma_alloc(uint32_t h, uint64_t size, uint64_t flags, uint64_t user_addr,
                     uint64_t* out) {
  if (size == 0 || size % kPageSize != 0 || size > abi::syscall::kVmoMaxSize) {
    return Status::InvalidArgs;
  }
  if ((flags & ~abi::syscall::kDmaContiguous) != 0) return Status::InvalidArgs;
  // Адрес результата проверяется до выделения: потом откатывать нечего.
  uint64_t zero = 0;
  if (!proc::copy_to_user(user_addr, &zero, sizeof zero)) return Status::BadAddress;
  {
    sync::SpinGuard guard(g_lock);
    Status s;
    Device* d = device_from(h, rights::kMap, &s);
    if (d == nullptr) return s;
    if (!dma_available(d)) return Status::NotSupported;
  }
  uint64_t pages = size / kPageSize;
  // Без IOMMU адрес для устройства — физический: буфер обязан быть непрерывным.
  bool contiguous = (flags & abi::syscall::kDmaContiguous) != 0 || !pci::iommu::active();
  auto* v = alloc_zeroed<Vmo>(sizeof(Vmo));
  auto* frames = alloc_zeroed<uint64_t>(pages * sizeof(uint64_t));
  if (v == nullptr || frames == nullptr) {
    if (v != nullptr) mm::kfree(v);
    if (frames != nullptr) mm::kfree(frames);
    return Status::NoMemory;
  }
  uint64_t got = 0;
  if (contiguous) {
    auto f = mm::alloc_frames(pages);
    if (f.is_ok()) {
      for (; got < pages; ++got) frames[got] = f.value() + got * kPageSize;
    }
  } else {
    for (; got < pages; ++got) {
      auto f = mm::alloc_frames();
      if (f.is_err()) break;
      frames[got] = f.value();
    }
  }
  if (got != pages) {
    for (uint64_t k = 0; k < got; ++k) mm::free_frames(frames[k]);
    mm::kfree(frames);
    mm::kfree(v);
    return Status::NoMemory;
  }
  for (uint64_t i = 0; i < pages; ++i) {
    __builtin_memset(mm::phys_to_virt<void>(frames[i]), 0, kPageSize);
  }
  v->base.type = Type::Vmo;
  v->size = size;
  v->pages = pages;
  v->frames = frames;

  uint64_t device_addr;
  {
    sync::SpinGuard guard(g_lock);
    Status s;
    Device* d = device_from(h, rights::kMap, &s);  // хендл могли закрыть
    if (d == nullptr) {
      v->base.refs = 1;
      release_object(&v->base);
      return s;
    }
    if (d->domain != nullptr) {
      v->iova = pci::iommu::map(d->domain, frames, pages, true);
      if (v->iova == 0) {
        v->base.refs = 1;
        release_object(&v->base);
        return Status::NoMemory;
      }
      device_addr = v->iova;
    } else {
      device_addr = frames[0];  // iommu=off
    }
    v->dma_dev = d;
    ++d->base.refs;
    s = install_new(&v->base, rights::kVmoDefault, out);
    if (s != Status::Ok) return s;  // install_new освободил VMO, а с ним ссылку и IOVA
    pci::set_command_bits(pci::function(d->pci_index), pci::kCommandBusMaster, 0);
  }
  if (!proc::copy_to_user(user_addr, &device_addr, sizeof device_addr)) return Status::BadAddress;
  return Status::Ok;
}

Status sys_device_vendor_cap(uint32_t h, uint64_t index, uint64_t user_buf, uint64_t cap,
                             uint64_t* out) {
  uint8_t bytes[256];
  uint32_t len = 0;
  {
    sync::SpinGuard guard(g_lock);
    Status s;
    Device* d = device_from(h, rights::kRead, &s);
    if (d == nullptr) return s;
    if (index > 255 ||
        !pci::vendor_cap(pci::function(d->pci_index), static_cast<uint32_t>(index), bytes,
                         sizeof bytes, &len)) {
      return Status::OutOfRange;
    }
  }
  *out = len;
  if (cap < len) return Status::BufferTooSmall;
  if (!proc::copy_to_user(user_buf, bytes, len)) return Status::BadAddress;
  return Status::Ok;
}

// --- для ядра: блочный слой ---------------------------------------------------------

Status kernel_recv(Cap* endpoint, void* buf, uint64_t cap, uint32_t* len, uint64_t deadline) {
  uint32_t got;
  return kernel_recv_caps(endpoint, buf, cap, len, nullptr, 0, &got, deadline);
}

Status kernel_recv_caps(Cap* endpoint, void* buf, uint64_t cap, uint32_t* len, Cap** caps,
                        uint32_t ncap, uint32_t* got, uint64_t deadline) {
  *got = 0;
  auto* ep = reinterpret_cast<Endpoint*>(endpoint->obj);
  for (;;) {
    bool irq = g_lock.lock();
    if (Message* m = ep->head) {
      *len = m->bytes;
      if (m->bytes > cap) {
        g_lock.unlock(irq);
        return Status::BufferTooSmall;
      }
      __builtin_memcpy(buf, m->data, m->bytes);
      ep->head = m->next;
      if (ep->head == nullptr) ep->tail = nullptr;
      --ep->count;
      m->next = nullptr;
      // Хендлы — ядру (сколько просили), остальное закроет destroy_message.
      for (uint32_t i = 0; i < m->ncaps && *got < ncap; ++i) {
        Cap* t = m->caps[i];
        if (t == nullptr) continue;
        t->where = Where::Detached;
        t->msg = nullptr;
        caps[(*got)++] = t;
        m->caps[i] = nullptr;
      }
      destroy_message(m);
      g_lock.unlock(irq);
      return Status::Ok;
    }
    if (ep->peer == nullptr) {
      g_lock.unlock(irq);
      return Status::PeerClosed;
    }
    if (deadline == 0) {
      g_lock.unlock(irq);
      return Status::WouldBlock;
    }
    if (deadline != ~uint64_t{0} && platform::now_ns() >= deadline) {
      g_lock.unlock(irq);
      return Status::TimedOut;
    }
    if (sched::interrupted()) {
      g_lock.unlock(irq);
      return Status::Interrupted;
    }
    sched::Thread* self = sched::current();
    ep->readers.push(self);
    sched::block_interruptible_until(deadline);
    g_lock.unlock(false);
    sched::schedule();
    sched::end_interruptible();
    {
      sync::SpinGuard guard(g_lock);
      ep->readers.remove(self);  // проснулись по сроку — ещё в очереди
    }
    if (irq) arch::x86_64::sti();
  }
}

Status create_kernel_vmo(uint64_t pages, Cap** out, uint64_t* frames_out) {
  auto* v = alloc_zeroed<Vmo>(sizeof(Vmo));
  auto* frames = alloc_zeroed<uint64_t>(pages * sizeof(uint64_t));
  if (v == nullptr || frames == nullptr) {
    if (v != nullptr) mm::kfree(v);
    if (frames != nullptr) mm::kfree(frames);
    return Status::NoMemory;
  }
  for (uint64_t i = 0; i < pages; ++i) {
    auto f = mm::alloc_frames();
    if (f.is_err()) {
      for (uint64_t k = 0; k < i; ++k) mm::free_frames(frames[k]);
      mm::kfree(frames);
      mm::kfree(v);
      return Status::NoMemory;
    }
    __builtin_memset(mm::phys_to_virt<void>(f.value()), 0, kPageSize);
    frames[i] = f.value();
    frames_out[i] = f.value();
  }
  v->base.type = Type::Vmo;
  v->size = pages * kPageSize;
  v->pages = pages;
  v->frames = frames;
  sync::SpinGuard guard(g_lock);
  Cap* c = new_cap(&v->base, rights::kVmoDefault & ~rights::kDuplicate, nullptr);
  if (c == nullptr) {
    v->base.refs = 1;
    release_object(&v->base);
    return Status::NoMemory;
  }
  *out = c;
  return Status::Ok;
}

Status clone_detached(Cap* cap, Cap** out) {
  sync::SpinGuard guard(g_lock);
  Cap* c = new_cap(cap->obj, cap->rights, nullptr);
  if (c == nullptr) return Status::NoMemory;
  *out = c;
  return Status::Ok;
}

void* acquire_device(uint32_t pci_index, pci::iommu::Domain** domain) {
  sync::SpinGuard guard(g_lock);
  Device* d = g_current_device[pci_index];
  if (d == nullptr) return nullptr;
  ++d->base.refs;
  *domain = d->domain;
  return d;
}

void release_device(void* device) {
  sync::SpinGuard guard(g_lock);
  release_object(&static_cast<Device*>(device)->base);
}

// --- узлы ФС -----------------------------------------------------------------------------

void set_fs_node_closed_hook(void (*hook)(uint64_t node)) { g_fs_node_closed = hook; }

namespace {

FsNode* new_fs_node(uint64_t node, bool dir) {
  auto* n = alloc_zeroed<FsNode>(sizeof(FsNode));
  if (n == nullptr) return nullptr;
  n->base.type = Type::FsNode;
  n->node = node;
  n->dir = dir;
  return n;
}

}  // namespace

Status create_fs_node(uint64_t node, bool dir, uint32_t r, Cap** out) {
  FsNode* n = new_fs_node(node, dir);
  if (n == nullptr) {
    if (g_fs_node_closed != nullptr) g_fs_node_closed(node);  // ссылка VFS не нужна
    return Status::NoMemory;
  }
  sync::SpinGuard guard(g_lock);
  Cap* c = new_cap(&n->base, r, nullptr);
  if (c == nullptr) {
    mm::kfree(n);
    return Status::NoMemory;
  }
  *out = c;
  return Status::Ok;
}

Status install_fs_node(uint64_t node, bool dir, uint32_t r, uint64_t* out) {
  FsNode* n = new_fs_node(node, dir);
  if (n == nullptr) {
    if (g_fs_node_closed != nullptr) g_fs_node_closed(node);  // ссылка VFS не нужна
    return Status::NoMemory;
  }
  sync::SpinGuard guard(g_lock);
  return install_new(&n->base, r, out);  // при ошибке объект освобождён (и хук вызван)
}

Status get_fs_node(uint32_t h, uint32_t need, uint64_t* node, bool* dir, uint32_t* r) {
  sync::SpinGuard guard(g_lock);
  Cap* c = lookup(me(), h);
  if (c == nullptr) return Status::BadHandle;
  if (c->obj->type != Type::FsNode) return Status::WrongType;
  if ((c->rights & need) != need) return Status::AccessDenied;
  auto* n = reinterpret_cast<FsNode*>(c->obj);
  *node = n->node;
  *dir = n->dir;
  *r = c->rights;
  return Status::Ok;
}

Status vmo_frames(Cap* vmo, uint64_t* out, uint64_t max, uint64_t* pages) {
  sync::SpinGuard guard(g_lock);
  if (vmo->obj->type != Type::Vmo) return Status::WrongType;
  auto* v = reinterpret_cast<Vmo*>(vmo->obj);
  if (v->mmio) return Status::WrongType;
  *pages = v->pages;
  for (uint64_t i = 0; i < v->pages && i < max; ++i) out[i] = v->frames[i];
  return Status::Ok;
}

// --- сеть ------------------------------------------------------------------------------

void set_socket_closed_hook(void (*hook)(uint64_t id)) { g_socket_closed = hook; }

Status create_net(uint32_t r, Cap** out) {
  auto* n = alloc_zeroed<NetObj>(sizeof(NetObj));
  if (n == nullptr) return Status::NoMemory;
  n->base.type = Type::Net;
  sync::SpinGuard guard(g_lock);
  Cap* c = new_cap(&n->base, r, nullptr);
  if (c == nullptr) {
    mm::kfree(n);
    return Status::NoMemory;
  }
  *out = c;
  return Status::Ok;
}

Status get_net(uint32_t h, uint32_t need, uint32_t* r) {
  sync::SpinGuard guard(g_lock);
  Cap* c = lookup(me(), h);
  if (c == nullptr) return Status::BadHandle;
  if (c->obj->type != Type::Net) return Status::WrongType;
  *r = c->rights;
  return (c->rights & need) == need ? Status::Ok : Status::AccessDenied;
}

Status install_socket(uint64_t id, uint64_t* out) {
  auto* so = alloc_zeroed<SocketObj>(sizeof(SocketObj));
  if (so == nullptr) {
    if (g_socket_closed != nullptr) g_socket_closed(id);
    return Status::NoMemory;
  }
  so->base.type = Type::Socket;
  so->id = id;
  sync::SpinGuard guard(g_lock);
  return install_new(&so->base, rights::kSocketDefault, out);
}

Status get_socket(uint32_t h, uint32_t need, uint64_t* id) {
  sync::SpinGuard guard(g_lock);
  Cap* c = lookup(me(), h);
  if (c == nullptr) return Status::BadHandle;
  if (c->obj->type != Type::Socket) return Status::WrongType;
  if ((c->rights & need) != need) return Status::AccessDenied;
  *id = reinterpret_cast<SocketObj*>(c->obj)->id;
  return Status::Ok;
}

void poll_sockets(bool (*ready)(uint64_t id, uint32_t* packet)) {
  // Готовность проверяется без g_lock (стек берёт свои блокировки), затем
  // пакет — под ним. Подписка однократная.
  for (uint32_t i = 0; i < kMaxSocketBinds; ++i) {
    uint64_t id;
    {
      sync::SpinGuard guard(g_lock);
      if (g_socket_binds[i] == nullptr) continue;
      id = g_socket_binds[i]->id;
    }
    uint32_t packet;
    if (!ready(id, &packet)) continue;
    sync::SpinGuard guard(g_lock);
    SocketObj* so = g_socket_binds[i];
    if (so == nullptr || so->id != id || so->bound == nullptr) continue;
    Port* port = so->bound;
    so->bound = nullptr;
    g_socket_binds[i] = nullptr;
    --port->armed;
    port_push(port, so->bound_key, packet);
    release_object(&port->base);
  }
}

// --- консоль ---------------------------------------------------------------------------

void set_console_input_probe(bool (*probe)()) { console_has_input = probe; }

Status create_console(uint32_t r, Cap** out) {
  auto* co = alloc_zeroed<ConsoleObj>(sizeof(ConsoleObj));
  if (co == nullptr) return Status::NoMemory;
  co->base.type = Type::Console;
  sync::SpinGuard guard(g_lock);
  Cap* c = new_cap(&co->base, r, nullptr);
  if (c == nullptr) {
    mm::kfree(co);
    return Status::NoMemory;
  }
  *out = c;
  return Status::Ok;
}

Status create_power(uint32_t r, Cap** out) {
  auto* o = alloc_zeroed<Object>(sizeof(Object));
  if (o == nullptr) return Status::NoMemory;
  o->type = Type::Power;
  sync::SpinGuard guard(g_lock);
  Cap* c = new_cap(o, r, nullptr);
  if (c == nullptr) {
    mm::kfree(o);
    return Status::NoMemory;
  }
  *out = c;
  return Status::Ok;
}

// --- экран (docs/abi/display.md) ---------------------------------------------------------

constinit abi::boot::Framebuffer g_fb = {};

Status create_display(const abi::boot::Framebuffer& fb, uint32_t r, Cap** out) {
  if (fb.base == 0) return Status::NotSupported;
  auto* o = alloc_zeroed<Object>(sizeof(Object));
  if (o == nullptr) return Status::NoMemory;
  o->type = Type::Display;
  sync::SpinGuard guard(g_lock);
  g_fb = fb;
  Cap* c = new_cap(o, r, nullptr);
  if (c == nullptr) {
    mm::kfree(o);
    return Status::NoMemory;
  }
  *out = c;
  return Status::Ok;
}

namespace {

Status display_check(uint32_t h, uint32_t need) {
  Cap* c = lookup(me(), h);
  if (c == nullptr) return Status::BadHandle;
  if (c->obj->type != Type::Display) return Status::WrongType;
  return (c->rights & need) == need ? Status::Ok : Status::AccessDenied;
}

bool display_format(uint32_t* out) {
  if (g_fb.format == abi::boot::PixelFormat::Bgrx8888) {
    *out = abi::syscall::kDisplayBgrx;
  } else if (g_fb.format == abi::boot::PixelFormat::Rgbx8888) {
    *out = abi::syscall::kDisplayRgbx;
  } else {
    return false;
  }
  return true;
}

}  // namespace

Status sys_display_info(uint32_t h, uint64_t user_out) {
  abi::syscall::DisplayInfo info{};
  {
    sync::SpinGuard guard(g_lock);
    if (Status s = display_check(h, rights::kRead); s != Status::Ok) return s;
    if (!display_format(&info.format)) return Status::NotSupported;
    info.width = g_fb.width;
    info.height = g_fb.height;
    info.pitch = g_fb.pitch;
    info.size = (uint64_t{g_fb.pitch} * g_fb.height + kPageSize - 1) / kPageSize * kPageSize;
  }
  return proc::copy_to_user(user_out, &info, sizeof info) ? Status::Ok : Status::BadAddress;
}

Status sys_display_map(uint32_t h, uint64_t* out) {
  uint64_t base, size;
  {
    sync::SpinGuard guard(g_lock);
    if (Status s = display_check(h, rights::kMap); s != Status::Ok) return s;
    uint32_t fmt;
    if (!display_format(&fmt) || g_fb.base % kPageSize != 0) return Status::NotSupported;
    base = g_fb.base;
    size = (uint64_t{g_fb.pitch} * g_fb.height + kPageSize - 1) / kPageSize * kPageSize;
  }
  uint64_t pages = size / kPageSize;
  auto* v = alloc_zeroed<Vmo>(sizeof(Vmo));
  auto* frames = alloc_zeroed<uint64_t>(pages * sizeof(uint64_t));
  if (v == nullptr || frames == nullptr) {
    if (v != nullptr) mm::kfree(v);
    if (frames != nullptr) mm::kfree(frames);
    return Status::NoMemory;
  }
  for (uint64_t i = 0; i < pages; ++i) frames[i] = base + i * kPageSize;
  v->base.type = Type::Vmo;
  v->size = size;
  v->pages = pages;
  v->frames = frames;
  v->mmio = true;
  v->wc = true;
  sync::SpinGuard guard(g_lock);
  return install_new(&v->base, rights::kRead | rights::kWrite | rights::kMap | rights::kTransfer, out);
}

// --- ввод (docs/abi/input.md) -------------------------------------------------------------

Status create_input(uint32_t r, Cap** out) {
  auto* o = alloc_zeroed<InputObj>(sizeof(InputObj));
  if (o == nullptr) return Status::NoMemory;
  o->base.type = Type::Input;
  sync::SpinGuard guard(g_lock);
  Cap* c = new_cap(&o->base, r, nullptr);
  if (c == nullptr) {
    mm::kfree(o);
    return Status::NoMemory;
  }
  *out = c;
  return Status::Ok;
}

Status sys_input_attach(uint32_t h, uint32_t vmo_h) {
  sync::SpinGuard guard(g_lock);
  proc::Process* p = me();
  Cap* c = lookup(p, h);
  Cap* vc = lookup(p, vmo_h);
  if (c == nullptr || vc == nullptr) return Status::BadHandle;
  if (c->obj->type != Type::Input || vc->obj->type != Type::Vmo) return Status::WrongType;
  if ((c->rights & rights::kWrite) == 0 || (vc->rights & rights::kWrite) == 0) return Status::AccessDenied;
  auto* v = reinterpret_cast<Vmo*>(vc->obj);
  if (v->mmio || v->size > abi::syscall::kInputRingMax || (v->size & (v->size - 1)) != 0) {
    return Status::InvalidArgs;
  }
  auto* hdr = mm::phys_to_virt<abi::syscall::InputRingHeader>(v->frames[0]);
  uint64_t room = (v->size - sizeof(abi::syscall::InputRingHeader)) / sizeof(abi::syscall::InputEvent);
  if (__builtin_memcmp(hdr->magic, "INPR", 4) != 0 || hdr->version != 1 || hdr->capacity == 0 ||
      (hdr->capacity & (hdr->capacity - 1)) != 0 || hdr->capacity > room) {
    return Status::InvalidArgs;
  }
  ++v->base.refs;
  if (g_input_ring != nullptr) release_object(&g_input_ring->base);
  g_input_ring = v;
  return Status::Ok;
}

// Из прерывания драйвера ввода: событие в кольцо, пакет подписчику.
void input_push(const abi::syscall::InputEvent& e) {
  sync::SpinGuard guard(g_lock);
  Vmo* v = g_input_ring;
  if (v == nullptr) return;
  auto* hdr = mm::phys_to_virt<abi::syscall::InputRingHeader>(v->frames[0]);
  uint32_t cap = hdr->capacity;  // проверено при attach; читатель может портить только head/tail
  uint32_t head = __atomic_load_n(&hdr->head, __ATOMIC_ACQUIRE);
  uint32_t tail = hdr->tail;
  if (tail - head >= cap) {
    ++hdr->dropped;
  } else {
    uint64_t off = sizeof(abi::syscall::InputRingHeader) + uint64_t{tail % cap} * sizeof(e);
    // Запись может пересечь границу страниц VMO (страницы не подряд) — по байтам.
    auto* src = reinterpret_cast<const uint8_t*>(&e);
    for (uint64_t i = 0; i < sizeof(e); ++i) {
      uint64_t at = off + i;
      mm::phys_to_virt<uint8_t>(v->frames[at / kPageSize])[at % kPageSize] = src[i];
    }
    __atomic_store_n(&hdr->tail, tail + 1, __ATOMIC_RELEASE);
  }
  if (Port* port = g_input_port) {
    g_input_port = nullptr;
    --port->armed;
    port_push(port, g_input_key, abi::syscall::kPacketInputReadable);
    release_object(&port->base);
  }
}

Status get_power(uint32_t h, uint32_t need) {
  sync::SpinGuard guard(g_lock);
  Cap* c = lookup(me(), h);
  if (c == nullptr) return Status::BadHandle;
  if (c->obj->type != Type::Power) return Status::WrongType;
  return (c->rights & need) == need ? Status::Ok : Status::AccessDenied;
}

Status get_console(uint32_t h, uint32_t need) {
  sync::SpinGuard guard(g_lock);
  Cap* c = lookup(me(), h);
  if (c == nullptr) return Status::BadHandle;
  if (c->obj->type != Type::Console) return Status::WrongType;
  return (c->rights & need) == need ? Status::Ok : Status::AccessDenied;
}

void console_readable() {
  sync::SpinGuard guard(g_lock);
  for (ConsoleObj*& b : g_console_binds) {
    if (b == nullptr || b->bound == nullptr) continue;
    Port* port = b->bound;
    b->bound = nullptr;
    --port->armed;
    port_push(port, b->bound_key, abi::syscall::kPacketConsoleReadable);
    release_object(&port->base);
    b = nullptr;
  }
}

// --- потоки ----------------------------------------------------------------------------

Status create_thread_obj(void** obj, uint64_t* handle) {
  auto* to = alloc_zeroed<ThreadObj>(sizeof(ThreadObj));
  if (to == nullptr) return Status::NoMemory;
  to->base.type = Type::Thread;
  to->base.refs = 1;  // ссылка самого потока
  sync::SpinGuard guard(g_lock);
  Status s = install_new(&to->base, rights::kDuplicate | rights::kTransfer | rights::kRead | rights::kWrite, handle);
  if (s != Status::Ok) {
    mm::kfree(to);
    return s;
  }
  *obj = to;
  return Status::Ok;
}

void thread_exited(void* obj, int64_t code) {
  auto* to = static_cast<ThreadObj*>(obj);
  sync::SpinGuard guard(g_lock);
  to->thread = nullptr;
  to->exited = true;
  to->code = code;
  if (Port* port = to->bound) {
    to->bound = nullptr;
    --port->armed;
    port_push(port, to->bound_key, abi::syscall::kPacketThreadExited, static_cast<uint64_t>(code));
    release_object(&port->base);
  }
  release_object(&to->base);
}

Status sys_thread_info(uint32_t h, uint64_t user_out) {
  abi::syscall::ThreadInfo info{};
  {
    sync::SpinGuard guard(g_lock);
    Cap* c = lookup(me(), h);
    if (c == nullptr) return Status::BadHandle;
    if (c->obj->type != Type::Thread) return Status::WrongType;
    if ((c->rights & rights::kRead) == 0) return Status::AccessDenied;
    auto* to = reinterpret_cast<ThreadObj*>(c->obj);
    info.state = to->exited ? abi::syscall::kThreadExited : abi::syscall::kThreadRunning;
    info.exit_code = to->code;
  }
  return proc::copy_to_user(user_out, &info, sizeof info) ? Status::Ok : Status::BadAddress;
}

// --- запуск: сообщение с хендлами вызывающего --------------------------------------------

Status send_from_table(Cap* endpoint, const void* bytes, uint64_t len, const uint32_t* handles,
                       uint32_t count) {
  if (len > kChannelMaxBytes || count > kChannelMaxHandles) return Status::InvalidArgs;
  auto* m = alloc_zeroed<Message>(sizeof(Message) + len);
  if (m == nullptr) return Status::NoMemory;
  __builtin_memcpy(m->data, bytes, len);
  m->bytes = static_cast<uint32_t>(len);
  Status result = Status::Ok;
  {
    sync::SpinGuard guard(g_lock);
    proc::Process* p = me();
    auto* ep = reinterpret_cast<Endpoint*>(endpoint->obj);
    Cap* moved[kChannelMaxHandles] = {};
    if (ep->peer == nullptr) result = Status::PeerClosed;
    // Всё проверяется до перемещения: при ошибке хендлы остаются у вызывающего.
    for (uint32_t i = 0; i < count && result == Status::Ok; ++i) {
      Cap* t = lookup(p, handles[i]);
      if (t == nullptr) {
        result = Status::BadHandle;
      } else if ((t->rights & rights::kTransfer) == 0) {
        result = Status::AccessDenied;
      } else {
        for (uint32_t j = 0; j < i; ++j) {
          if (moved[j] == t) result = Status::InvalidArgs;
        }
        moved[i] = t;
      }
    }
    if (result == Status::Ok) {
      for (uint32_t i = 0; i < count; ++i) {
        p->ipc.table.remove(moved[i]->handle);
        moved[i]->where = Where::Message;
        moved[i]->owner = nullptr;
        moved[i]->msg = m;
        m->caps[i] = moved[i];
      }
      m->ncaps = count;
      Endpoint* peer = ep->peer;
      if (peer->tail != nullptr) {
        peer->tail->next = m;
      } else {
        peer->head = m;
      }
      peer->tail = m;
      ++peer->count;
      if (sched::Thread* t = peer->readers.pop()) sched::wake(t);
      fire(peer, abi::syscall::kPacketChannelReadable);
      m = nullptr;
    }
  }
  if (m != nullptr) mm::kfree(m);
  return result;
}

// --- процессы ---------------------------------------------------------------------------

Status install_process(proc::Process* p, uint64_t* handle) {
  auto* po = alloc_zeroed<ProcessObj>(sizeof(ProcessObj));
  if (po == nullptr) return Status::NoMemory;
  po->base.type = Type::Process;
  po->proc = p;  // ссылку отдаёт вызывающий
  sync::SpinGuard guard(g_lock);
  Cap* c = new_cap(&po->base, rights::kDuplicate | rights::kTransfer | rights::kRead | rights::kWrite |
                                  rights::kSignal,
                   nullptr);
  if (c == nullptr) {
    mm::kfree(po);
    return Status::NoMemory;
  }
  proc::Process* self = me();
  auto h = self->ipc.table.insert(c);
  if (h.is_err()) {
    unlink(c);
    mm::kfree(c);
    mm::kfree(po);
    return Status::NoMemory;
  }
  c->where = Where::Table;
  c->owner = self;
  c->handle = h.value();
  if (!__atomic_load_n(&p->dead, __ATOMIC_ACQUIRE)) {
    po->next_watch = static_cast<ProcessObj*>(p->watchers);
    p->watchers = po;
  }
  *handle = h.value();
  return Status::Ok;
}

Status get_process(uint32_t h, uint32_t need, proc::Process** out) {
  sync::SpinGuard guard(g_lock);
  Cap* c = lookup(me(), h);
  if (c == nullptr) return Status::BadHandle;
  if (c->obj->type != Type::Process) return Status::WrongType;
  if ((c->rights & need) != need) return Status::AccessDenied;
  *out = reinterpret_cast<ProcessObj*>(c->obj)->proc;
  proc::get_process_ref(*out);
  return Status::Ok;
}

void process_exited(proc::Process* p) {
  sync::SpinGuard guard(g_lock);
  __atomic_store_n(&p->dead, true, __ATOMIC_RELEASE);
  auto* po = static_cast<ProcessObj*>(p->watchers);
  p->watchers = nullptr;
  while (po != nullptr) {
    ProcessObj* next = po->next_watch;
    po->next_watch = nullptr;
    if (Port* port = po->bound) {
      po->bound = nullptr;
      --port->armed;
      port_push(port, po->bound_key, abi::syscall::kPacketProcessExited, static_cast<uint64_t>(p->exit_code));
      release_object(&port->base);
    }
    po = next;
  }
}

// --- Job --------------------------------------------------------------------------------

namespace {

Job* new_job(Job* parent) {
  auto* j = alloc_zeroed<Job>(sizeof(Job));
  if (j == nullptr) return nullptr;
  j->base.type = Type::Job;
  j->parent = parent;
  if (parent != nullptr) {
    j->limits = parent->limits;
    ++parent->base.refs;
  }
  return j;
}

bool over(uint64_t used, uint64_t limit) { return limit != 0 && used >= limit; }

}  // namespace

Job* root_job() {
  sync::SpinGuard guard(g_lock);
  if (g_root_job == nullptr) {
    g_root_job = new_job(nullptr);
    if (g_root_job == nullptr) panic("ipc: no memory for the root job");
    g_root_job->base.refs = 1;  // ядро держит корень всегда
  }
  return g_root_job;
}

bool job_contains(const Job* ancestor, const Job* j) {
  for (; j != nullptr; j = j->parent) {
    if (j == ancestor) return true;
  }
  return false;
}

Status job_enter(Job* j) {
  sync::SpinGuard guard(g_lock);
  for (Job* a = j; a != nullptr; a = a->parent) {
    if (over(a->processes, a->limits.processes) || over(a->threads, a->limits.threads)) return Status::NoSpace;
  }
  for (Job* a = j; a != nullptr; a = a->parent) {
    ++a->processes;
    ++a->threads;
  }
  ++j->base.refs;
  return Status::Ok;
}

void job_leave(Job* j) {
  sync::SpinGuard guard(g_lock);
  for (Job* a = j; a != nullptr; a = a->parent) {
    --a->processes;
    if (a->processes == 0 && a->bound != nullptr) {
      Port* port = a->bound;
      a->bound = nullptr;
      --port->armed;
      port_push(port, a->bound_key, abi::syscall::kPacketJobEmpty);
      release_object(&port->base);
    }
  }
  release_object(&j->base);
}

Status job_thread_enter(Job* j) {
  sync::SpinGuard guard(g_lock);
  for (Job* a = j; a != nullptr; a = a->parent) {
    if (over(a->threads, a->limits.threads)) return Status::NoSpace;
  }
  for (Job* a = j; a != nullptr; a = a->parent) ++a->threads;
  return Status::Ok;
}

void job_thread_leave(Job* j) {
  sync::SpinGuard guard(g_lock);
  for (Job* a = j; a != nullptr; a = a->parent) --a->threads;
}

Status get_job(uint32_t h, uint32_t need, Job** out) {
  sync::SpinGuard guard(g_lock);
  Cap* c = lookup(me(), h);
  if (c == nullptr) return Status::BadHandle;
  if (c->obj->type != Type::Job) return Status::WrongType;
  if ((c->rights & need) != need) return Status::AccessDenied;
  *out = reinterpret_cast<Job*>(c->obj);
  ++(*out)->base.refs;
  return Status::Ok;
}

void job_put(Job* j) {
  sync::SpinGuard guard(g_lock);
  release_object(&j->base);
}

void job_get(Job* j) {
  sync::SpinGuard guard(g_lock);
  ++j->base.refs;
}

Status create_job_cap(Job* j, Cap** out) {
  sync::SpinGuard guard(g_lock);
  Cap* c = new_cap(&j->base, rights::kDuplicate | rights::kTransfer | rights::kRead | rights::kWrite | rights::kSpawn,
                   nullptr);
  if (c == nullptr) return Status::NoMemory;
  *out = c;
  return Status::Ok;
}

Status sys_job_create(uint32_t parent_h, uint64_t* out) {
  *out = 0;
  Job* parent;
  Status s = get_job(parent_h, rights::kSpawn, &parent);
  if (s != Status::Ok) return s;
  Job* j = new_job(parent);
  if (j == nullptr) {
    job_put(parent);
    return Status::NoMemory;
  }
  sync::SpinGuard guard(g_lock);
  release_object(&parent->base);  // ссылку держит сам дочерний Job
  Cap* c = new_cap(&j->base, rights::kDuplicate | rights::kTransfer | rights::kRead | rights::kWrite | rights::kSpawn,
                   nullptr);
  if (c == nullptr) {
    release_object(&parent->base);
    mm::kfree(j);
    return Status::NoMemory;
  }
  proc::Process* self = me();
  auto h = self->ipc.table.insert(c);
  if (h.is_err()) {
    release_detached(c);  // освобождает и Job (с его ссылкой на родителя)
    return Status::NoMemory;
  }
  c->where = Where::Table;
  c->owner = self;
  c->handle = h.value();
  *out = h.value();
  return Status::Ok;
}

Status sys_job_info(uint32_t h, uint64_t user_out) {
  abi::process::JobInfo info{};
  {
    sync::SpinGuard guard(g_lock);
    Cap* c = lookup(me(), h);
    if (c == nullptr) return Status::BadHandle;
    if (c->obj->type != Type::Job) return Status::WrongType;
    if ((c->rights & rights::kRead) == 0) return Status::AccessDenied;
    auto* j = reinterpret_cast<Job*>(c->obj);
    info.processes = j->processes;
    info.threads = j->threads;
    info.memory_pages = j->memory_pages;
    info.cpu_ns = j->cpu_ns;
  }
  return proc::copy_to_user(user_out, &info, sizeof info) ? Status::Ok : Status::BadAddress;
}

Status set_foreground(Job* j) {
  Job* old;
  {
    sync::SpinGuard guard(g_lock);
    ++j->base.refs;
    old = g_foreground;
    g_foreground = j;
    if (old != nullptr) release_object(&old->base);
  }
  return Status::Ok;
}

bool in_foreground(const proc::Process* p) {
  sync::SpinGuard guard(g_lock);
  return g_foreground != nullptr && p != nullptr && job_contains(g_foreground, p->job);
}

void with_foreground(void (*fn)(Job* j, void* ctx), void* ctx) {
  sync::SpinGuard guard(g_lock);
  if (g_foreground != nullptr) fn(g_foreground, ctx);
}

// --- лимиты Job (process.md §7), приоритеты (§8) -----------------------------------------

namespace {

uint64_t clip(uint64_t want, uint64_t parent) {
  if (want == 0) return parent;
  return parent != 0 && parent < want ? parent : want;
}

}  // namespace

void job_set_limits(Job* j, const abi::process::JobLimits& l) {
  sync::SpinGuard guard(g_lock);
  abi::process::JobLimits parent{};
  if (j->parent != nullptr) parent = j->parent->limits;
  j->limits.memory_pages = clip(l.memory_pages, parent.memory_pages);
  j->limits.processes = static_cast<uint32_t>(clip(l.processes, parent.processes));
  j->limits.threads = static_cast<uint32_t>(clip(l.threads, parent.threads));
  j->limits.handles_per_process = static_cast<uint32_t>(clip(l.handles_per_process, parent.handles_per_process));
  j->limits.max_priority = static_cast<uint32_t>(clip(l.max_priority, parent.max_priority));
  j->limits.cpu_ns = clip(l.cpu_ns, parent.cpu_ns);
}

Status job_charge_memory(Job* j, uint64_t pages) {
  sync::SpinGuard guard(g_lock);
  for (Job* a = j; a != nullptr; a = a->parent) {
    if (a->limits.memory_pages != 0 && a->memory_pages + pages > a->limits.memory_pages) return Status::NoMemory;
  }
  for (Job* a = j; a != nullptr; a = a->parent) a->memory_pages += pages;
  return Status::Ok;
}

void job_uncharge_memory(Job* j, uint64_t pages) {
  sync::SpinGuard guard(g_lock);
  uncharge_locked(j, pages);
}

Job* job_charge_cpu(Job* j, uint64_t ns) {
  // Без блокировки (из планировщика): предки неизменны, счётчики атомарны.
  Job* over = nullptr;
  for (Job* a = j; a != nullptr; a = a->parent) {
    uint64_t used = __atomic_add_fetch(&a->cpu_ns, ns, __ATOMIC_RELAXED);
    uint64_t limit = __atomic_load_n(&a->limits.cpu_ns, __ATOMIC_RELAXED);
    if (limit != 0 && used > limit) over = a;  // самый верхний превышенный
  }
  return over;
}

uint32_t job_max_priority(const Job* j) {
  uint32_t max = abi::process::kPriorityMax;
  for (; j != nullptr; j = j->parent) {
    uint32_t l = __atomic_load_n(&j->limits.max_priority, __ATOMIC_RELAXED);
    if (l != 0 && l < max) max = l;
  }
  return max;
}

uint32_t job_handle_limit(const Job* j) {
  uint32_t lim = 0;
  for (; j != nullptr; j = j->parent) {
    uint32_t l = j->limits.handles_per_process;
    if (l != 0 && (lim == 0 || l < lim)) lim = l;
  }
  return lim;
}

void thread_obj_attach(void* obj, sched::Thread* t) {
  sync::SpinGuard guard(g_lock);
  auto* to = static_cast<ThreadObj*>(obj);
  if (!to->exited) to->thread = t;
}

Status thread_set_priority(uint32_t h, uint32_t priority) {
  sync::SpinGuard guard(g_lock);
  Cap* c = lookup(me(), h);
  if (c == nullptr) return Status::BadHandle;
  if (c->obj->type != Type::Thread) return Status::WrongType;
  if ((c->rights & rights::kWrite) == 0) return Status::AccessDenied;
  auto* to = reinterpret_cast<ThreadObj*>(c->obj);
  if (to->thread != nullptr) sched::set_priority(to->thread, priority);
  return Status::Ok;
}

}  // namespace kernel::ipc
