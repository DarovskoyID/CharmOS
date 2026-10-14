#include "block.hpp"

#include <abi/block.hpp>
#include <arch/x86_64/paging.hpp>

#include "../core/log.hpp"
#include "../ipc/ipc.hpp"
#include "../mm/layout.hpp"
#include "../pci/iommu.hpp"
#include "../platform/platform.hpp"
#include "../sched/sched.hpp"
#include "../sync/spinlock.hpp"
#include "../sync/wait.hpp"

namespace kernel::block {

namespace {

using abi::block::Completion;
using abi::block::Request;
using abi::block::RingHeader;
using arch::x86_64::kPageSize;

struct Disk {
  uint32_t pci_index;
  uint32_t block_size;
  uint64_t block_count;
  uint32_t flags;
  bool used;
  // Под io: состояние текущего запуска драйвера.
  sync::Mutex io{"block.io"};
  bool online;
  volatile bool alive;  // сбрасывает serve, не беря io (драйвер ушёл)
  volatile bool failed;
  uint64_t ring[abi::block::kRingPages];
  ipc::Cap* ring_cap;
  ipc::Cap* notify;
  pci::iommu::Domain* domain;  // nullptr — без IOMMU (iommu=off): адрес = физический
  void* device;
  uint64_t next_id;
};

constinit sync::Spinlock g_lock{sync::LockRank::Block, "block"};
constinit Disk g_disks[kMaxDisks] = {};
constinit uint32_t g_count = 0;

RingHeader* header(Disk& d) { return mm::phys_to_virt<RingHeader>(d.ring[0]); }

Request* sq_entry(Disk& d, uint32_t i) {
  uint64_t off = abi::block::kSqOffset + (i % abi::block::kRingEntries) * sizeof(Request);
  return mm::phys_to_virt<Request>(d.ring[off / kPageSize] + off % kPageSize);
}

Completion* cq_entry(Disk& d, uint32_t i) {
  uint64_t off = abi::block::kCqOffset + (i % abi::block::kRingEntries) * sizeof(Completion);
  return mm::phys_to_virt<Completion>(d.ring[off / kPageSize] + off % kPageSize);
}

template <typename T>
T load(const T* p) {
  return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}
template <typename T>
void store(T* p, T v) {
  __atomic_store_n(p, v, __ATOMIC_RELEASE);
}

Disk* find(uint32_t pci_index) {
  sync::SpinGuard guard(g_lock);
  for (uint32_t i = 0; i < g_count; ++i) {
    if (g_disks[i].pci_index == pci_index) return &g_disks[i];
  }
  return nullptr;
}

// Один запрос целиком: в кольцо, ожидание ответа. io захвачен. Возвращает
// false, если драйвер ушёл до ответа (запрос надо повторить).
bool run_once(Disk& d, Request req, const uint64_t* pages, Status* result) {
  uint64_t iova_base = pci::iommu::kKernelIovaBase;
  for (uint32_t j = 0; j < req.segment_count; ++j) {
    if (d.domain != nullptr) {
      if (!pci::iommu::map_at(d.domain, iova_base + j * kPageSize, pages[j],
                              req.op == abi::block::kOpRead)) {
        if (j > 0) pci::iommu::unmap(d.domain, iova_base, j);
        *result = Status::NoMemory;
        return true;
      }
      req.segments[j].iova = iova_base + j * kPageSize;
    } else {
      req.segments[j].iova = pages[j];
    }
  }
  req.id = ++d.next_id;

  RingHeader* h = header(d);
  uint32_t tail = h->sq_tail;
  *sq_entry(d, tail) = req;
  store(&h->sq_tail, tail + 1);
  if (load(&h->driver_wants_kick) != 0) {
    store(&h->driver_wants_kick, 0u);
    char none = 0;
    (void)ipc::kernel_send(d.notify, &none, 0, nullptr, 0);
  }

  bool done = false;
  while (!done && d.alive) {
    uint32_t head = h->cq_head;
    while (head != load(&h->cq_tail)) {
      Completion c = *cq_entry(d, head);
      ++head;
      store(&h->cq_head, head);
      if (c.id == req.id) {
        *result = static_cast<Status>(c.status);
        done = true;
      }
    }
    if (done) break;
    store(&h->kernel_wants_kick, 1u);
    if (head != load(&h->cq_tail)) continue;
    char buf[8];
    uint32_t len;
    Status s = ipc::kernel_recv(d.notify, buf, sizeof buf, &len, platform::now_ns() + 100'000'000);
    if (s == Status::PeerClosed) sched::sleep_ns(10'000'000);  // драйвер уходит: ждём serve
  }
  store(&h->kernel_wants_kick, 0u);
  if (d.domain != nullptr && req.segment_count != 0) {
    pci::iommu::unmap(d.domain, iova_base, req.segment_count);
  }
  return done;
}

Status submit(uint32_t disk, Request req, const uint64_t* pages) {
  if (disk >= disk_count()) return Status::InvalidArgs;
  Disk& d = g_disks[disk];
  d.io.lock();
  for (;;) {
    if (d.failed) {
      d.io.unlock();
      return Status::IoError;
    }
    if (!d.online || !d.alive) {
      d.io.unlock();
      sched::sleep_ns(10'000'000);
      d.io.lock();
      continue;
    }
    Status result = Status::IoError;
    if (run_once(d, req, pages, &result)) {
      d.io.unlock();
      return result;
    }
    kprint("block: disk{}: driver gone, request {} will be repeated\n", disk, d.next_id);
  }
}

}  // namespace

uint32_t disk_count() { return __atomic_load_n(&g_count, __ATOMIC_ACQUIRE); }

bool disk_info(uint32_t disk, DiskInfo* out) {
  if (disk >= disk_count()) return false;
  Disk& d = g_disks[disk];
  *out = {d.block_size, d.block_count, (d.flags & abi::block::kHelloReadOnly) != 0,
          d.online && d.alive, d.failed};
  return true;
}

bool wait_online(uint32_t disk, uint64_t deadline_ns) {
  for (;;) {
    DiskInfo info;
    if (disk_info(disk, &info) && (info.online || info.failed)) return info.online;
    if (platform::now_ns() >= deadline_ns) return false;
    sched::sleep_ns(10'000'000);
  }
}

Status transfer(uint32_t disk, bool write, uint64_t lba, const uint64_t* pages, uint32_t count) {
  if (disk >= disk_count() || count == 0 || count > kMaxPages) return Status::InvalidArgs;
  Disk& d = g_disks[disk];
  uint64_t per_page = kPageSize / d.block_size;
  if (lba > d.block_count || count * per_page > d.block_count - lba) return Status::OutOfRange;
  if (write && (d.flags & abi::block::kHelloReadOnly) != 0) return Status::AccessDenied;
  Request req = {};
  req.op = write ? abi::block::kOpWrite : abi::block::kOpRead;
  req.segment_count = count;
  req.lba = lba;
  req.blocks = static_cast<uint32_t>(count * per_page);
  for (uint32_t i = 0; i < count; ++i) req.segments[i].bytes = static_cast<uint32_t>(kPageSize);
  return submit(disk, req, pages);
}

Status flush(uint32_t disk) {
  if (disk >= disk_count()) return Status::InvalidArgs;
  if ((g_disks[disk].flags & abi::block::kHelloFlush) == 0) return Status::Ok;
  Request req = {};
  req.op = abi::block::kOpFlush;
  return submit(disk, req, nullptr);
}

void serve(uint32_t pci_index, ipc::Cap* kernel_end, const char* name) {
  abi::block::BlockHello hello;
  uint32_t len = 0;
  if (ipc::kernel_recv(kernel_end, &hello, sizeof hello, &len, ~uint64_t{0}) != Status::Ok) return;
  bool ok = len == sizeof hello && __builtin_memcmp(hello.magic, abi::block::kHelloMagic, 4) == 0 &&
            (hello.block_size == 512 || hello.block_size == 4096) && hello.block_count != 0 &&
            hello.max_blocks >= kPageSize / hello.block_size * kMaxPages;
  Disk* d = find(pci_index);
  if (ok && d != nullptr && (d->block_size != hello.block_size ||
                             d->block_count != hello.block_count)) {
    ok = false;  // другой диск после перезапуска — запросы старого не повторять
  }
  if (!ok) {
    kprint("block: {}: unacceptable disk parameters\n", name);
    (void)ipc::kernel_send(kernel_end, "reject", 6, nullptr, 0);
    return;
  }

  pci::iommu::Domain* domain = nullptr;
  void* device = ipc::acquire_device(pci_index, &domain);
  if (device == nullptr) return;
  uint64_t frames[abi::block::kRingPages];
  ipc::Cap* ring = nullptr;
  ipc::Cap* ring_for_driver = nullptr;
  ipc::Cap* notify = nullptr;
  ipc::Cap* notify_for_driver = nullptr;
  if (ipc::create_kernel_vmo(abi::block::kRingPages, &ring, frames) != Status::Ok ||
      ipc::clone_detached(ring, &ring_for_driver) != Status::Ok ||
      ipc::create_channel(&notify, &notify_for_driver) != Status::Ok) {
    kprint("block: {}: out of memory\n", name);
    if (ring != nullptr) ipc::release_detached(ring);
    if (ring_for_driver != nullptr) ipc::release_detached(ring_for_driver);
    ipc::release_device(device);
    return;
  }
  ipc::Cap* caps[2] = {ring_for_driver, notify_for_driver};
  if (ipc::kernel_send(kernel_end, "ring", 4, caps, 2) != Status::Ok) {
    ipc::release_detached(ring_for_driver);
    ipc::release_detached(notify_for_driver);
    ipc::release_detached(ring);
    ipc::release_detached(notify);
    ipc::release_device(device);
    return;
  }

  if (d == nullptr) {
    sync::SpinGuard guard(g_lock);
    if (g_count == kMaxDisks) {
      kprint("block: too many disks\n");
    } else {
      d = &g_disks[g_count];
      d->pci_index = pci_index;
      d->block_size = hello.block_size;
      d->block_count = hello.block_count;
      d->flags = hello.flags;
      d->used = true;
      __atomic_store_n(&g_count, g_count + 1, __ATOMIC_RELEASE);
    }
  }
  if (d != nullptr) {
    d->io.lock();
    for (uint64_t i = 0; i < abi::block::kRingPages; ++i) d->ring[i] = frames[i];
    d->ring_cap = ring;
    d->notify = notify;
    d->domain = domain;
    d->device = device;
    d->alive = true;
    d->online = true;
    d->io.unlock();
    kprint("block: disk{} is {}: {} blocks of {} bytes{}\n", static_cast<uint32_t>(d - g_disks),
           name, hello.block_count, hello.block_size,
           (hello.flags & abi::block::kHelloReadOnly) != 0 ? ", read-only" : "");
  }

  // Ждём ухода драйвера: его конец стартового канала закроется.
  char buf[64];
  while (ipc::kernel_recv(kernel_end, buf, sizeof buf, &len, ~uint64_t{0}) != Status::PeerClosed) {
  }
  if (d != nullptr) {
    d->alive = false;
    d->io.lock();  // ждущий запрос заметит alive == false и отпустит io
    d->online = false;
    d->ring_cap = nullptr;
    d->notify = nullptr;
    d->domain = nullptr;
    d->device = nullptr;
    d->io.unlock();
  }
  ipc::release_detached(ring);
  ipc::release_detached(notify);
  ipc::release_device(device);
}

void driver_failed(uint32_t pci_index) {
  if (Disk* d = find(pci_index)) {
    d->failed = true;
    kprint("block: disk{} failed\n", static_cast<uint32_t>(d - g_disks));
  }
}

}  // namespace kernel::block
