// Драйвер сетевого устройства virtio (docs/specs/virtio-1.2.pdf): современный
// интерфейс PCI (§4.1), разделённые очереди (§2.7), virtio-net (§5.1):
// очередь 0 — приём, 1 — передача; перед кадром — virtio_net_hdr (12 байт,
// с VERSION_1 есть num_buffers), отдельным дескриптором цепочки.
// С ядром — по протоколу docs/abi/net.md.
#include <abi/net.hpp>

#include "../../tests/check.hpp"

using namespace charm;
namespace abin = abi::net;

namespace {

constexpr uint8_t kCapCommon = 1;
constexpr uint8_t kCapNotify = 2;
constexpr uint8_t kCapDevice = 4;
constexpr uint64_t kDeviceFeatureSelect = 0x00, kDeviceFeature = 0x04;
constexpr uint64_t kDriverFeatureSelect = 0x08, kDriverFeature = 0x0c;
constexpr uint64_t kConfigMsixVector = 0x10, kDeviceStatus = 0x14;
constexpr uint64_t kQueueSelect = 0x16, kQueueSize = 0x18, kQueueMsixVector = 0x1a;
constexpr uint64_t kQueueEnable = 0x1c, kQueueNotifyOff = 0x1e;
constexpr uint64_t kQueueDesc = 0x20, kQueueDriver = 0x28, kQueueDevice = 0x30;
constexpr uint16_t kNoVector = 0xffff;
constexpr uint8_t kStatusAck = 1, kStatusDriver = 2, kStatusDriverOk = 4, kStatusFeaturesOk = 8;
constexpr uint64_t kFeatureMac = 1ull << 5;      // §5.1.3
constexpr uint64_t kFeatureStatus = 1ull << 16;  // §5.1.3
constexpr uint64_t kFeatureVersion1 = 1ull << 32;
constexpr uint64_t kFeatureAccessPlatform = 1ull << 33;
constexpr uint16_t kLinkUp = 1;  // VIRTIO_NET_S_LINK_UP

struct Desc {
  uint64_t addr;
  uint32_t len;
  uint16_t flags;
  uint16_t next;
};
static_assert(sizeof(Desc) == 16);
constexpr uint16_t kDescNext = 1, kDescWrite = 2;
constexpr uint32_t kHdr = 12;  // virtio_net_hdr с num_buffers

constexpr uint16_t kQ = 64;  // размер очереди; цепочка — 2 дескриптора
constexpr uint32_t kBufferSize = 2048;
constexpr uint32_t kBuffers = 256;

template <typename T>
volatile T& reg(uint64_t base, uint64_t off) {
  return *reinterpret_cast<volatile T*>(base + off);
}
void fence() { __atomic_thread_fence(__ATOMIC_SEQ_CST); }

struct Queue {
  volatile Desc* desc;
  volatile uint16_t* avail;
  volatile uint8_t* used;
  uint64_t iova;
  uint16_t avail_idx, used_seen;
  uint64_t notify;
  uint32_t buffer[kQ / 2];  // буфер пула для цепочки i
};

void set_desc(volatile Desc& e, uint64_t a, uint32_t l, uint16_t f, uint16_t n) {
  e.addr = a;
  e.len = l;
  e.flags = f;
  e.next = n;
}

}  // namespace

extern "C" int64_t charm_main(Handle kernel) {
  char buf[16];
  Handle hs[2];
  uint32_t n, nh;
  EXPECT(read_msg(kernel, buf, sizeof buf, hs, 2, &n, &nh) == Status::Ok);
  EXPECT(same(buf, n, "device") && nh == 1);
  Handle dev = hs[0];
  abi::syscall::DeviceInfo info;
  EXPECT(syscall2(Number::DeviceInfo, dev, reinterpret_cast<uint64_t>(&info)).ok());
  EXPECT((info.flags & abi::syscall::kDeviceDma) != 0 && info.irq_count >= 1);

  uint64_t common = 0, notify_base = 0, devcfg = 0, bar_addr[6] = {};
  uint32_t notify_mult = 0;
  for (uint64_t idx = 0;; ++idx) {
    uint8_t cap[64];
    auto r = syscall5(Number::DeviceVendorCap, dev, idx, reinterpret_cast<uint64_t>(cap), sizeof cap, 0);
    if (r.status == Status::OutOfRange) break;
    EXPECT(r.ok() && r.value >= 16);
    uint8_t type = cap[3], bar = cap[4];
    if (bar > 5 || info.bar_kind[bar] == abi::syscall::kBarAbsent || info.bar_kind[bar] == abi::syscall::kBarIo) continue;
    bool wanted = (type == kCapCommon && common == 0) || (type == kCapNotify && notify_base == 0) ||
                  (type == kCapDevice && devcfg == 0);
    if (!wanted) continue;
    if (bar_addr[bar] == 0) {
      auto b = syscall2(Number::DeviceMapBar, dev, bar);
      EXPECT(b.ok());
      auto m = vmo_map(static_cast<Handle>(b.value), rights::kRead | rights::kWrite);
      EXPECT(m.ok());
      bar_addr[bar] = m.value;
    }
    uint32_t off = static_cast<uint32_t>(cap[8] | cap[9] << 8 | cap[10] << 16 | cap[11] << 24);
    uint64_t addr = bar_addr[bar] + off;
    if (type == kCapCommon) common = addr;
    if (type == kCapDevice) devcfg = addr;
    if (type == kCapNotify) {
      EXPECT(r.value >= 20);
      notify_mult = static_cast<uint32_t>(cap[16] | cap[17] << 8 | cap[18] << 16 | cap[19] << 24);
      notify_base = addr;
    }
  }
  EXPECT(common != 0 && notify_base != 0 && devcfg != 0);

  // §3.1.1
  reg<uint8_t>(common, kDeviceStatus) = 0;
  while (reg<uint8_t>(common, kDeviceStatus) != 0) yield();
  reg<uint8_t>(common, kDeviceStatus) = kStatusAck | kStatusDriver;
  reg<uint32_t>(common, kDeviceFeatureSelect) = 0;
  uint64_t offered = reg<uint32_t>(common, kDeviceFeature);
  reg<uint32_t>(common, kDeviceFeatureSelect) = 1;
  offered |= uint64_t{reg<uint32_t>(common, kDeviceFeature)} << 32;
  EXPECT((offered & kFeatureVersion1) != 0 && (offered & kFeatureMac) != 0);
  uint64_t accepted = offered & (kFeatureVersion1 | kFeatureAccessPlatform | kFeatureMac | kFeatureStatus);
  reg<uint32_t>(common, kDriverFeatureSelect) = 0;
  reg<uint32_t>(common, kDriverFeature) = static_cast<uint32_t>(accepted);
  reg<uint32_t>(common, kDriverFeatureSelect) = 1;
  reg<uint32_t>(common, kDriverFeature) = static_cast<uint32_t>(accepted >> 32);
  reg<uint8_t>(common, kDeviceStatus) = kStatusAck | kStatusDriver | kStatusFeaturesOk;
  EXPECT((reg<uint8_t>(common, kDeviceStatus) & kStatusFeaturesOk) != 0);

  // Пул буферов (отдаётся ядру), заголовки virtio, кольца ядра.
  uint64_t pool_iova, hdr_iova, ring_iova;
  auto pool = dma_alloc(dev, uint64_t{kBuffers} * kBufferSize, abi::syscall::kDmaContiguous, &pool_iova);
  EXPECT(pool.ok());
  auto hdrs = dma_alloc(dev, 4096, 0, &hdr_iova);
  EXPECT(hdrs.ok());
  auto rings = vmo_create(4096);
  EXPECT(rings.ok());
  auto rings_map = vmo_map(static_cast<Handle>(rings.value), rights::kRead | rights::kWrite);
  EXPECT(rings_map.ok());
  (void)ring_iova;

  auto irq = syscall2(Number::DeviceIrq, dev, 0);
  EXPECT(irq.ok());
  Handle irq_h = static_cast<Handle>(irq.value);
  reg<uint16_t>(common, kConfigMsixVector) = kNoVector;
  Queue q[2] = {};
  for (uint16_t i = 0; i < 2; ++i) {
    reg<uint16_t>(common, kQueueSelect) = i;
    uint16_t qs = reg<uint16_t>(common, kQueueSize);
    EXPECT(qs >= kQ);
    reg<uint16_t>(common, kQueueSize) = kQ;
    auto m = dma_alloc(dev, 4096, 0, &q[i].iova);
    EXPECT(m.ok());
    auto mm = vmo_map(static_cast<Handle>(m.value), rights::kRead | rights::kWrite);
    EXPECT(mm.ok());
    // Дескрипторы 1 КиБ, avail с 1024, used с 2048 (§2.7: выравнивание 16/2/4).
    q[i].desc = reinterpret_cast<volatile Desc*>(mm.value);
    q[i].avail = reinterpret_cast<volatile uint16_t*>(mm.value + 1024);
    q[i].used = reinterpret_cast<volatile uint8_t*>(mm.value + 2048);
    reg<uint16_t>(common, kQueueMsixVector) = 0;
    EXPECT(reg<uint16_t>(common, kQueueMsixVector) == 0);
    reg<uint64_t>(common, kQueueDesc) = q[i].iova;
    reg<uint64_t>(common, kQueueDriver) = q[i].iova + 1024;
    reg<uint64_t>(common, kQueueDevice) = q[i].iova + 2048;
    q[i].notify = notify_base + uint64_t{reg<uint16_t>(common, kQueueNotifyOff)} * notify_mult;
    reg<uint16_t>(common, kQueueEnable) = 1;
  }
  reg<uint8_t>(common, kDeviceStatus) = kStatusAck | kStatusDriver | kStatusFeaturesOk | kStatusDriverOk;

  abin::NetHello hello = {};
  for (int i = 0; i < 4; ++i) hello.magic[i] = abin::kHelloMagic[i];
  for (int i = 0; i < 6; ++i) hello.mac[i] = reg<uint8_t>(devcfg, static_cast<uint64_t>(i));
  hello.mtu = 1500;
  hello.buffer_size = kBufferSize;
  hello.buffer_count = kBuffers;
  Handle give[2] = {static_cast<Handle>(pool.value), static_cast<Handle>(rings.value)};
  EXPECT(channel_write(kernel, &hello, sizeof hello, give, 2) == Status::Ok);
  EXPECT(read_msg(kernel, buf, sizeof buf, hs, 2, &n, &nh) == Status::Ok);
  if (same(buf, n, "reject")) return 2;
  EXPECT(same(buf, n, "ok") && nh == 1);
  Handle note = hs[0];
  auto* h = reinterpret_cast<abin::NetRingHeader*>(rings_map.value);
  auto slot = [&](uint64_t base, uint32_t i) -> abin::Slot* {
    return reinterpret_cast<abin::Slot*>(rings_map.value + base) + i % abin::kRingEntries;
  };
  auto link = [&] {
    bool up = (accepted & kFeatureStatus) == 0 || (reg<uint16_t>(devcfg, 6) & kLinkUp) != 0;
    __atomic_store_n(&h->link_up, up ? 1u : 0u, __ATOMIC_RELEASE);
  };
  link();
  print("virtio_net: ready\n");

  auto kernel_kick = [&] {
    if (__atomic_load_n(&h->kernel_wants_kick, __ATOMIC_ACQUIRE) != 0) {
      __atomic_store_n(&h->kernel_wants_kick, 0u, __ATOMIC_RELAXED);
      char none = 0;
      (void)channel_write(note, &none, 0, nullptr, 0);
    }
  };
  // Цепочка c: дескрипторы 2c (заголовок) и 2c + 1 (буфер).
  uint32_t busy[2] = {0, 0};  // занятые цепочки (битовые карты по 32)
  auto post = [&](int qi, uint32_t buffer, uint32_t len) -> bool {
    Queue& qq = q[qi];
    uint32_t c = 0;
    while (c < kQ / 2 && (busy[qi] & (1u << c)) != 0) ++c;
    if (c == kQ / 2) return false;
    busy[qi] |= 1u << c;
    qq.buffer[c] = buffer;
    uint64_t hdr = hdr_iova + (uint64_t(qi) * 32 + c) * 16;
    uint16_t d = static_cast<uint16_t>(2 * c);
    bool rx = qi == 0;
    set_desc(qq.desc[d], hdr, kHdr, kDescNext | (rx ? kDescWrite : 0), static_cast<uint16_t>(d + 1));
    set_desc(qq.desc[d + 1], pool_iova + uint64_t(buffer) * kBufferSize, rx ? kBufferSize : len,
             rx ? kDescWrite : 0, 0);
    qq.avail[2 + qq.avail_idx % kQ] = d;
    fence();
    ++qq.avail_idx;
    qq.avail[1] = qq.avail_idx;
    fence();
    reg<uint16_t>(qq.notify, 0) = static_cast<uint16_t>(qi);
    return true;
  };
  if (!hdrs.ok()) return 5;
  auto hmap = vmo_map(static_cast<Handle>(hdrs.value), rights::kRead | rights::kWrite);
  EXPECT(hmap.ok());  // заголовки передачи — нули (без offload)

  auto port = port_create();
  EXPECT(port.ok());
  Handle port_h = static_cast<Handle>(port.value);
  bool irq_armed = false, kick_armed = false, kernel_armed = false;
  for (;;) {
    bool any = false;
    // Ядро дало буферы под приём и кадры на отправку.
    while (h->rx_free_head != __atomic_load_n(&h->rx_free_tail, __ATOMIC_ACQUIRE)) {
      abin::Slot s = *slot(abin::kRxFreeOffset, h->rx_free_head);
      if (s.buffer >= kBuffers || !post(0, s.buffer, 0)) break;
      __atomic_store_n(&h->rx_free_head, h->rx_free_head + 1, __ATOMIC_RELEASE);
      any = true;
    }
    while (h->tx_head != __atomic_load_n(&h->tx_tail, __ATOMIC_ACQUIRE)) {
      abin::Slot s = *slot(abin::kTxOffset, h->tx_head);
      if (s.buffer >= kBuffers || s.length > kBufferSize || !post(1, s.buffer, s.length)) break;
      __atomic_store_n(&h->tx_head, h->tx_head + 1, __ATOMIC_RELEASE);
      any = true;
    }
    // Готовое устройством (§2.7.8).
    for (int qi = 0; qi < 2; ++qi) {
      Queue& qq = q[qi];
      fence();
      uint16_t used_idx = *reinterpret_cast<volatile uint16_t*>(qq.used + 2);
      while (qq.used_seen != used_idx) {
        volatile uint8_t* e = qq.used + 4 + (qq.used_seen % kQ) * 8;
        uint32_t id = *reinterpret_cast<volatile uint32_t*>(e);
        uint32_t written = *reinterpret_cast<volatile uint32_t*>(e + 4);
        ++qq.used_seen;
        uint32_t c = id / 2;
        if (c >= kQ / 2 || (busy[qi] & (1u << c)) == 0) continue;
        busy[qi] &= ~(1u << c);
        if (qi == 0) {
          uint32_t t = h->rx_done_tail;
          uint32_t len = written > kHdr ? written - kHdr : 0;
          *slot(abin::kRxDoneOffset, t) = {qq.buffer[c], len, len != 0 ? 0 : static_cast<int32_t>(Status::IoError), 0};
          __atomic_store_n(&h->rx_done_tail, t + 1, __ATOMIC_RELEASE);
        } else {
          uint32_t t = h->tx_done_tail;
          *slot(abin::kTxDoneOffset, t) = {qq.buffer[c], 0, 0, 0};
          __atomic_store_n(&h->tx_done_tail, t + 1, __ATOMIC_RELEASE);
        }
        any = true;
      }
    }
    if (any) {
      link();
      kernel_kick();
      continue;
    }
    __atomic_store_n(&h->driver_wants_kick, 1u, __ATOMIC_SEQ_CST);
    if (h->rx_free_head != __atomic_load_n(&h->rx_free_tail, __ATOMIC_ACQUIRE) ||
        h->tx_head != __atomic_load_n(&h->tx_tail, __ATOMIC_ACQUIRE)) {
      continue;
    }
    if (!irq_armed) irq_armed = object_bind(port_h, irq_h, 1) == Status::Ok;
    if (!kick_armed) kick_armed = object_bind(port_h, note, 2) == Status::Ok;
    if (!kernel_armed) kernel_armed = object_bind(port_h, kernel, 3) == Status::Ok;
    PortPacket pk;
    if (port_wait(port_h, &pk, now_ns() + 100'000'000) != Status::Ok) {
      link();
      continue;
    }
    if (pk.key == 1) {
      irq_armed = false;
      (void)syscall1(Number::InterruptAck, irq_h);
    } else if (pk.key == 2) {
      kick_armed = false;
      Handle none_h[1];
      while (channel_read(note, buf, sizeof buf, none_h, 1, 0).ok()) {
      }
    } else if (pk.key == 3) {
      kernel_armed = false;
      if (pk.type == abi::syscall::kPacketPeerClosed) return 0;
    }
  }
}
