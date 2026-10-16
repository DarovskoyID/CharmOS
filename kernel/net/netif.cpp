#include "netif.hpp"

#include <abi/net.hpp>

#include "../core/log.hpp"
#include "../ipc/ipc.hpp"
#include "../mm/heap.hpp"
#include "../mm/layout.hpp"
#include "../platform/platform.hpp"
#include "../sched/sched.hpp"
#include "../sync/wait.hpp"

namespace kernel::net {

namespace {

namespace abin = abi::net;
using abin::NetRingHeader;
using abin::Slot;

constexpr uint64_t kPage = 4096;
constexpr uint64_t kMaxPoolPages = 1024 * 4096 / kPage * 2;  // до 1024 буферов по 8 КиБ

struct Iface {
  sync::Mutex lock{"net.if"};  // состояние запуска и отправка
  bool up;                     // рукопожатие прошло, драйвер жив
  uint8_t mac[6];
  uint16_t mtu;
  uint32_t buffer_size;
  uint32_t buffer_count;
  uint64_t* pool;  // фреймы пула
  uint64_t ring[2];  // фреймы VMO колец (2 страницы, net.md §3)
  ipc::Cap* notify;
  bool* kernel_owns;  // буфер у ядра (свободен для отправки)
};

constinit Iface g_if = {};

NetRingHeader* header() { return mm::phys_to_virt<NetRingHeader>(g_if.ring[0]); }
Slot* ring_slot(uint64_t base, uint32_t i) {
  uint64_t off = base + (i % abin::kRingEntries) * sizeof(Slot);  // слот не пересекает страницу
  return mm::phys_to_virt<Slot>(g_if.ring[off / kPage] + off % kPage);
}

// Байты буфера b (буфер может пересекать границу страницы пула).
void copy_to_buffer(uint32_t b, const uint8_t* src, uint32_t len) {
  uint64_t off = uint64_t{b} * g_if.buffer_size;
  for (uint32_t done = 0; done < len;) {
    uint64_t pos = off + done;
    uint32_t chunk = static_cast<uint32_t>(kPage - pos % kPage);
    if (chunk > len - done) chunk = len - done;
    __builtin_memcpy(mm::phys_to_virt<uint8_t>(g_if.pool[pos / kPage]) + pos % kPage, src + done, chunk);
    done += chunk;
  }
}

void copy_from_buffer(uint32_t b, uint8_t* dst, uint32_t len) {
  uint64_t off = uint64_t{b} * g_if.buffer_size;
  for (uint32_t done = 0; done < len;) {
    uint64_t pos = off + done;
    uint32_t chunk = static_cast<uint32_t>(kPage - pos % kPage);
    if (chunk > len - done) chunk = len - done;
    __builtin_memcpy(dst + done, mm::phys_to_virt<uint8_t>(g_if.pool[pos / kPage]) + pos % kPage, chunk);
    done += chunk;
  }
}

template <typename T>
T load(const T* p) {
  return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}
template <typename T>
void store(T* p, T v) {
  __atomic_store_n(p, v, __ATOMIC_RELEASE);
}

void kick_driver() {
  NetRingHeader* h = header();
  if (load(&h->driver_wants_kick) != 0) {
    store(&h->driver_wants_kick, 0u);
    char none = 0;
    (void)ipc::kernel_send(g_if.notify, &none, 0, nullptr, 0);
  }
}

// Отдать драйверу пустой буфер для приёма (под lock).
void give_rx(uint32_t b) {
  NetRingHeader* h = header();
  uint32_t t = h->rx_free_tail;
  *ring_slot(abin::kRxFreeOffset, t) = {b, 0, 0, 0};
  store(&h->rx_free_tail, t + 1);
  g_if.kernel_owns[b] = false;
}

}  // namespace

bool link_up() { return g_if.up && load(&header()->link_up) != 0; }

void dump() {
  sync::MutexGuard guard(g_if.lock);
  if (!g_if.up) return;
  NetRingHeader* h = header();
  uint32_t owned = 0;
  for (uint32_t b = 0; b < g_if.buffer_count; ++b) owned += g_if.kernel_owns[b] ? 1 : 0;
  kprint("net: rx_free {}..{} rx_done {}..{} tx {}..{} tx_done {}..{} kick drv {} kern {} link {} kernel owns {}/{}\n",
         load(&h->rx_free_head), h->rx_free_tail, h->rx_done_head, load(&h->rx_done_tail), load(&h->tx_head),
         h->tx_tail, h->tx_done_head, load(&h->tx_done_tail), load(&h->driver_wants_kick),
         load(&h->kernel_wants_kick), load(&h->link_up), owned, g_if.buffer_count);
}

bool mac(uint8_t out[6]) {
  if (!g_if.up) return false;
  __builtin_memcpy(out, g_if.mac, 6);
  return true;
}

uint16_t mtu() { return g_if.mtu; }

Status send_frame(const void* frame, uint32_t len) {
  sync::MutexGuard guard(g_if.lock);
  if (!g_if.up || load(&header()->link_up) == 0) return Status::NetUnreachable;
  if (len > g_if.buffer_size || len < 14) return Status::InvalidArgs;
  NetRingHeader* h = header();
  if (h->tx_tail - load(&h->tx_head) >= abin::kRingEntries) return Status::NoMemory;
  uint32_t b = 0;
  while (b < g_if.buffer_count && !g_if.kernel_owns[b]) ++b;
  if (b == g_if.buffer_count) return Status::NoMemory;
  copy_to_buffer(b, static_cast<const uint8_t*>(frame), len);
  g_if.kernel_owns[b] = false;
  uint32_t t = h->tx_tail;
  *ring_slot(abin::kTxOffset, t) = {b, len, 0, 0};
  store(&h->tx_tail, t + 1);
  kick_driver();
  return Status::Ok;
}

void serve(uint32_t pci_index, ipc::Cap* kernel_end, const char* name) {
  (void)pci_index;
  abin::NetHello hello;
  uint32_t len = 0, got = 0;
  ipc::Cap* caps[2] = {};
  if (ipc::kernel_recv_caps(kernel_end, &hello, sizeof hello, &len, caps, 2, &got, ~uint64_t{0}) !=
      Status::Ok) {
    return;
  }
  auto release_caps = [&] {
    for (uint32_t i = 0; i < got; ++i) ipc::release_detached(caps[i]);
  };
  bool ok = len == sizeof hello && got == 2 && __builtin_memcmp(hello.magic, abin::kHelloMagic, 4) == 0 &&
            hello.buffer_size >= abin::kMinBufferSize && hello.buffer_size % 64 == 0 &&
            hello.buffer_size <= 8192 && hello.buffer_count >= abin::kMinBuffers &&
            hello.buffer_count <= abin::kMaxBuffers && hello.mtu >= 576 &&
            hello.mtu + 14u <= hello.buffer_size;
  uint64_t pool_pages = 0, ring_pages = 0, ring_frames[2] = {};
  auto pool_mem = mm::kmalloc(kMaxPoolPages * sizeof(uint64_t));
  auto owns_mem = mm::kmalloc(abin::kMaxBuffers);
  if (pool_mem.is_err() || owns_mem.is_err()) ok = false;
  auto* pool = pool_mem.is_ok() ? static_cast<uint64_t*>(pool_mem.value()) : nullptr;
  if (ok) {
    ok = ipc::vmo_frames(caps[0], pool, kMaxPoolPages, &pool_pages) == Status::Ok &&
         pool_pages * kPage >= uint64_t{hello.buffer_size} * hello.buffer_count &&
         pool_pages <= kMaxPoolPages &&
         ipc::vmo_frames(caps[1], ring_frames, 2, &ring_pages) == Status::Ok &&
         ring_pages * kPage >= abin::kRingBytes;
  }
  ipc::Cap* notify = nullptr;
  ipc::Cap* notify_driver = nullptr;
  if (ok && ipc::create_channel(&notify, &notify_driver) != Status::Ok) ok = false;
  if (!ok) {
    kprint("net: {}: unacceptable interface parameters\n", name);
    (void)ipc::kernel_send(kernel_end, "reject", 6, nullptr, 0);
    release_caps();
    if (pool != nullptr) mm::kfree(pool);
    if (owns_mem.is_ok()) mm::kfree(owns_mem.value());
    return;
  }
  if (ipc::kernel_send(kernel_end, "ok", 2, &notify_driver, 1) != Status::Ok) {
    ipc::release_detached(notify_driver);
    ipc::release_detached(notify);
    release_caps();
    mm::kfree(pool);
    mm::kfree(owns_mem.value());
    return;
  }

  {
    sync::MutexGuard guard(g_if.lock);
    __builtin_memcpy(g_if.mac, hello.mac, 6);
    g_if.mtu = hello.mtu;
    g_if.buffer_size = hello.buffer_size;
    g_if.buffer_count = hello.buffer_count;
    g_if.pool = pool;
    g_if.ring[0] = ring_frames[0];
    g_if.ring[1] = ring_frames[1];
    g_if.notify = notify;
    g_if.kernel_owns = static_cast<bool*>(owns_mem.value());
    for (uint32_t b = 0; b < hello.buffer_count; ++b) g_if.kernel_owns[b] = true;
    for (uint32_t b = 0; b < hello.buffer_count / 2 && b < abin::kRingEntries; ++b) give_rx(b);
    g_if.up = true;
    kick_driver();
  }
  kprint("net: interface {} mac {x}:{x}:{x}:{x}:{x}:{x} mtu {}\n", name, hello.mac[0], hello.mac[1],
         hello.mac[2], hello.mac[3], hello.mac[4], hello.mac[5], hello.mtu);

  auto frame_mem = mm::kmalloc(hello.buffer_size);
  auto* frame = frame_mem.is_ok() ? static_cast<uint8_t*>(frame_mem.value()) : nullptr;
  for (;;) {
    NetRingHeader* h = header();
    bool any = false;
    // Принятые кадры: в стек, буфер — снова на приём.
    for (uint32_t head = h->rx_done_head; head != load(&h->rx_done_tail); ++head) {
      Slot s = *ring_slot(abin::kRxDoneOffset, head);
      store(&h->rx_done_head, head + 1);
      any = true;
      if (s.buffer >= hello.buffer_count) continue;
      if (s.status == 0 && frame != nullptr && s.length >= 14 && s.length <= hello.buffer_size) {
        copy_from_buffer(s.buffer, frame, s.length);
        input(frame, s.length);
      }
      sync::MutexGuard guard(g_if.lock);
      give_rx(s.buffer);
    }
    // Отправленные: буфер снова у ядра.
    for (uint32_t head = h->tx_done_head; head != load(&h->tx_done_tail); ++head) {
      Slot s = *ring_slot(abin::kTxDoneOffset, head);
      store(&h->tx_done_head, head + 1);
      any = true;
      sync::MutexGuard guard(g_if.lock);
      if (s.buffer < hello.buffer_count) g_if.kernel_owns[s.buffer] = true;
    }
    if (any) {
      sync::MutexGuard guard(g_if.lock);
      kick_driver();
      continue;
    }
    store(&h->kernel_wants_kick, 1u);
    if (h->rx_done_head != load(&h->rx_done_tail) || h->tx_done_head != load(&h->tx_done_tail)) continue;
    char buf[8];
    (void)ipc::kernel_recv(notify, buf, sizeof buf, &len, platform::now_ns() + 50'000'000);
    // Драйвер ушёл: его конец стартового канала закрыт.
    char msg[16];
    Status s = ipc::kernel_recv(kernel_end, msg, sizeof msg, &len, 0);
    if (s == Status::PeerClosed) break;
  }

  {
    sync::MutexGuard guard(g_if.lock);
    g_if.up = false;
    g_if.pool = nullptr;
    g_if.ring[0] = g_if.ring[1] = 0;
    g_if.notify = nullptr;
    g_if.kernel_owns = nullptr;
  }
  kprint("net: interface {} down\n", name);
  if (frame != nullptr) mm::kfree(frame);
  ipc::release_detached(notify);
  release_caps();
  mm::kfree(pool);
  mm::kfree(owns_mem.value());
}

}  // namespace kernel::net
