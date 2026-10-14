// Драйвер блочного устройства virtio (docs/specs/virtio-1.2.pdf): современный
// интерфейс PCI (§4.1), разделённая очередь (§2.7), virtio-blk (§5.2).
// С ядром говорит по протоколу docs/abi/block.md.
#include <abi/block.hpp>

#include "../../tests/check.hpp"

using namespace charm;
namespace blk = abi::block;

namespace {

// §4.1.4: virtio_pci_cap.
constexpr uint8_t kCapCommon = 1;
constexpr uint8_t kCapNotify = 2;
constexpr uint8_t kCapDevice = 4;

// §4.1.4.3: virtio_pci_common_cfg.
constexpr uint64_t kDeviceFeatureSelect = 0x00;
constexpr uint64_t kDeviceFeature = 0x04;
constexpr uint64_t kDriverFeatureSelect = 0x08;
constexpr uint64_t kDriverFeature = 0x0c;
constexpr uint64_t kConfigMsixVector = 0x10;
constexpr uint64_t kDeviceStatus = 0x14;
constexpr uint64_t kQueueSelect = 0x16;
constexpr uint64_t kQueueSize = 0x18;
constexpr uint64_t kQueueMsixVector = 0x1a;
constexpr uint64_t kQueueEnable = 0x1c;
constexpr uint64_t kQueueNotifyOff = 0x1e;
constexpr uint64_t kQueueDesc = 0x20;
constexpr uint64_t kQueueDriver = 0x28;
constexpr uint64_t kQueueDevice = 0x30;
constexpr uint16_t kNoVector = 0xffff;  // §4.1.5.1.2

// §2.1.
constexpr uint8_t kStatusAcknowledge = 1;
constexpr uint8_t kStatusDriver = 2;
constexpr uint8_t kStatusDriverOk = 4;
constexpr uint8_t kStatusFeaturesOk = 8;
constexpr uint8_t kStatusFailed = 128;

// Биты возможностей: §6 (общие), §5.2.3 (блочные).
constexpr uint64_t kFeatureRo = 1ull << 5;
constexpr uint64_t kFeatureFlush = 1ull << 9;
constexpr uint64_t kFeatureVersion1 = 1ull << 32;
constexpr uint64_t kFeatureAccessPlatform = 1ull << 33;

// §2.7.5: virtq_desc.
struct Desc {
  uint64_t addr;
  uint32_t len;
  uint16_t flags;
  uint16_t next;
};
static_assert(sizeof(Desc) == 16);
constexpr uint16_t kDescNext = 1;
constexpr uint16_t kDescWrite = 2;

// §5.2.6: virtio_blk_req (заголовок; данные и байт статуса — отдельными дескрипторами).
struct BlkHeader {
  uint32_t type;
  uint32_t reserved;
  uint64_t sector;
};
static_assert(sizeof(BlkHeader) == 16);
constexpr uint32_t kBlkIn = 0;
constexpr uint32_t kBlkOut = 1;
constexpr uint32_t kBlkFlush = 4;
constexpr uint8_t kBlkOk = 0;
constexpr uint8_t kBlkUnsupp = 2;

// Раскладка очереди в одной странице DMA (§2.7: выравнивание 16 / 2 / 4).
constexpr uint16_t kQueueMax = 128;
constexpr uint64_t kAvailOffset = 2048;  // после 128 дескрипторов
constexpr uint64_t kUsedOffset = 2560;
constexpr uint32_t kDescPerRequest = 2 + blk::kMaxSegments;
constexpr uint32_t kMaxInflight = kQueueMax / kDescPerRequest;  // 16

struct Slot {
  bool busy;
  uint64_t id;
};

struct Driver {
  uint64_t common = 0;
  uint64_t notify = 0;
  uint64_t device_cfg = 0;
  uint64_t bar_addr[6] = {};
  uint16_t queue_size = 0;
  volatile Desc* desc = nullptr;
  volatile uint16_t* avail = nullptr;  // flags, idx, ring[]
  volatile uint8_t* used = nullptr;    // flags, idx, ring[] по 8 байт
  uint64_t queue_iova = 0;
  volatile uint8_t* req_mem = nullptr;  // заголовки и байты статуса запросов
  uint64_t req_iova = 0;
  uint16_t avail_idx = 0;
  uint16_t used_seen = 0;
  Slot slots[kMaxInflight] = {};
};

template <typename T>
volatile T& reg(uint64_t base, uint64_t off) {
  return *reinterpret_cast<volatile T*>(base + off);
}

void fence() { __atomic_thread_fence(__ATOMIC_SEQ_CST); }

uint64_t read_features(Driver& d) {
  reg<uint32_t>(d.common, kDeviceFeatureSelect) = 0;
  uint64_t lo = reg<uint32_t>(d.common, kDeviceFeature);
  reg<uint32_t>(d.common, kDeviceFeatureSelect) = 1;
  uint64_t hi = reg<uint32_t>(d.common, kDeviceFeature);
  return lo | (hi << 32);
}

void write_features(Driver& d, uint64_t f) {
  reg<uint32_t>(d.common, kDriverFeatureSelect) = 0;
  reg<uint32_t>(d.common, kDriverFeature) = static_cast<uint32_t>(f);
  reg<uint32_t>(d.common, kDriverFeatureSelect) = 1;
  reg<uint32_t>(d.common, kDriverFeature) = static_cast<uint32_t>(f >> 32);
}

void set_desc(volatile Desc& e, uint64_t addr, uint32_t len, uint16_t flags, uint16_t next) {
  e.addr = addr;
  e.len = len;
  e.flags = flags;
  e.next = next;
}

void submit(Driver& d, uint32_t s, const blk::Request& r) {
  auto* hdr = reinterpret_cast<volatile BlkHeader*>(d.req_mem + s * 32);
  volatile uint8_t* status = d.req_mem + s * 32 + 16;
  hdr->type = r.op == blk::kOpRead ? kBlkIn : r.op == blk::kOpWrite ? kBlkOut : kBlkFlush;
  hdr->reserved = 0;
  hdr->sector = r.op == blk::kOpFlush ? 0 : r.lba;  // блоки ядра — 512 байт, как сектора
  *status = 0xff;
  uint16_t first = static_cast<uint16_t>(s * kDescPerRequest);
  uint16_t i = first;
  set_desc(d.desc[i], d.req_iova + s * 32, 16, kDescNext, static_cast<uint16_t>(i + 1));
  ++i;
  uint32_t nseg = r.op == blk::kOpFlush ? 0 : r.segment_count;
  for (uint32_t k = 0; k < nseg; ++k, ++i) {
    uint16_t flags = kDescNext | (r.op == blk::kOpRead ? kDescWrite : 0);
    set_desc(d.desc[i], r.segments[k].iova, r.segments[k].bytes, flags, static_cast<uint16_t>(i + 1));
  }
  set_desc(d.desc[i], d.req_iova + s * 32 + 16, 1, kDescWrite, 0);
  d.slots[s] = {true, r.id};
  // §2.7.13: элемент кольца, барьер, затем idx.
  d.avail[2 + d.avail_idx % d.queue_size] = first;
  fence();
  ++d.avail_idx;
  d.avail[1] = d.avail_idx;
  fence();
  reg<uint16_t>(d.notify, 0) = 0;  // номер очереди 0
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
  EXPECT((info.flags & abi::syscall::kDeviceDma) != 0);
  EXPECT(info.irq_count >= 1);

  Driver d;
  // Раскладка из vendor-specific capability (§4.1.4), первая подходящая каждого типа.
  uint32_t notify_mult = 0;
  uint64_t notify_base = 0;
  for (uint64_t idx = 0;; ++idx) {
    uint8_t cap[64];
    auto r = syscall5(Number::DeviceVendorCap, dev, idx, reinterpret_cast<uint64_t>(cap), sizeof cap, 0);
    if (r.status == Status::OutOfRange) break;
    EXPECT(r.ok() && r.value >= 16);
    uint8_t type = cap[3], bar = cap[4];
    if (bar > 5 || info.bar_kind[bar] == abi::syscall::kBarAbsent ||
        info.bar_kind[bar] == abi::syscall::kBarIo) {
      continue;
    }
    uint32_t offset = static_cast<uint32_t>(cap[8] | cap[9] << 8 | cap[10] << 16 | cap[11] << 24);
    bool wanted = (type == kCapCommon && d.common == 0) || (type == kCapNotify && d.notify == 0) ||
                  (type == kCapDevice && d.device_cfg == 0);
    if (!wanted) continue;
    if (d.bar_addr[bar] == 0) {
      auto b = syscall2(Number::DeviceMapBar, dev, bar);
      EXPECT(b.ok());
      auto m = vmo_map(static_cast<Handle>(b.value), rights::kRead | rights::kWrite);
      EXPECT(m.ok());
      d.bar_addr[bar] = m.value;
    }
    uint64_t addr = d.bar_addr[bar] + offset;
    if (type == kCapCommon) d.common = addr;
    if (type == kCapDevice) d.device_cfg = addr;
    if (type == kCapNotify) {
      EXPECT(r.value >= 20);
      notify_mult = static_cast<uint32_t>(cap[16] | cap[17] << 8 | cap[18] << 16 | cap[19] << 24);
      notify_base = addr;
      d.notify = addr;
    }
  }
  EXPECT(d.common != 0 && d.notify != 0 && d.device_cfg != 0);

  // §3.1.1: инициализация.
  reg<uint8_t>(d.common, kDeviceStatus) = 0;
  while (reg<uint8_t>(d.common, kDeviceStatus) != 0) yield();
  reg<uint8_t>(d.common, kDeviceStatus) = kStatusAcknowledge;
  reg<uint8_t>(d.common, kDeviceStatus) = kStatusAcknowledge | kStatusDriver;
  uint64_t offered = read_features(d);
  if ((offered & kFeatureVersion1) == 0) {
    reg<uint8_t>(d.common, kDeviceStatus) = kStatusFailed;
    print("virtio_blk: legacy-only device, not supported\n");
    return 1;
  }
  uint64_t accepted = offered & (kFeatureVersion1 | kFeatureAccessPlatform | kFeatureRo |
                                 kFeatureFlush);
  write_features(d, accepted);
  reg<uint8_t>(d.common, kDeviceStatus) = kStatusAcknowledge | kStatusDriver | kStatusFeaturesOk;
  EXPECT((reg<uint8_t>(d.common, kDeviceStatus) & kStatusFeaturesOk) != 0);

  // Очередь 0 (§4.1.5.1.3) и прерывание MSI-X 0 для неё.
  reg<uint16_t>(d.common, kConfigMsixVector) = kNoVector;
  reg<uint16_t>(d.common, kQueueSelect) = 0;
  uint16_t qs = reg<uint16_t>(d.common, kQueueSize);
  EXPECT(qs != 0);
  d.queue_size = qs < kQueueMax ? qs : kQueueMax;
  EXPECT(d.queue_size >= kDescPerRequest);
  reg<uint16_t>(d.common, kQueueSize) = d.queue_size;
  auto qmem = dma_alloc(dev, 4096, 0, &d.queue_iova);
  EXPECT(qmem.ok());
  auto qmap = vmo_map(static_cast<Handle>(qmem.value), rights::kRead | rights::kWrite);
  EXPECT(qmap.ok());
  d.desc = reinterpret_cast<volatile Desc*>(qmap.value);
  d.avail = reinterpret_cast<volatile uint16_t*>(qmap.value + kAvailOffset);
  d.used = reinterpret_cast<volatile uint8_t*>(qmap.value + kUsedOffset);
  auto rmem = dma_alloc(dev, 4096, 0, &d.req_iova);
  EXPECT(rmem.ok());
  auto rmap = vmo_map(static_cast<Handle>(rmem.value), rights::kRead | rights::kWrite);
  EXPECT(rmap.ok());
  d.req_mem = reinterpret_cast<volatile uint8_t*>(rmap.value);

  auto irq = syscall2(Number::DeviceIrq, dev, 0);
  EXPECT(irq.ok());
  Handle irq_h = static_cast<Handle>(irq.value);
  reg<uint16_t>(d.common, kQueueMsixVector) = 0;
  EXPECT(reg<uint16_t>(d.common, kQueueMsixVector) == 0);
  reg<uint64_t>(d.common, kQueueDesc) = d.queue_iova;
  reg<uint64_t>(d.common, kQueueDriver) = d.queue_iova + kAvailOffset;
  reg<uint64_t>(d.common, kQueueDevice) = d.queue_iova + kUsedOffset;
  d.notify = notify_base + uint64_t{reg<uint16_t>(d.common, kQueueNotifyOff)} * notify_mult;
  reg<uint16_t>(d.common, kQueueEnable) = 1;
  reg<uint8_t>(d.common, kDeviceStatus) =
      kStatusAcknowledge | kStatusDriver | kStatusFeaturesOk | kStatusDriverOk;

  // §5.2.4: ёмкость в секторах по 512 байт.
  uint64_t capacity = reg<uint32_t>(d.device_cfg, 0) | uint64_t{reg<uint32_t>(d.device_cfg, 4)} << 32;

  // Рукопожатие с ядром (docs/abi/block.md §2).
  blk::BlockHello hello = {};
  for (int i = 0; i < 4; ++i) hello.magic[i] = blk::kHelloMagic[i];
  hello.block_size = 512;
  hello.block_count = capacity;
  hello.max_blocks = blk::kMaxSegments * 8;
  hello.flags = ((accepted & kFeatureRo) != 0 ? blk::kHelloReadOnly : 0) |
                ((accepted & kFeatureFlush) != 0 ? blk::kHelloFlush : 0);
  EXPECT(channel_write(kernel, &hello, sizeof hello, nullptr, 0) == Status::Ok);
  EXPECT(read_msg(kernel, buf, sizeof buf, hs, 2, &n, &nh) == Status::Ok);
  if (same(buf, n, "reject")) return 2;
  EXPECT(same(buf, n, "ring") && nh == 2);
  auto ring_map = vmo_map(hs[0], rights::kRead | rights::kWrite);
  EXPECT(ring_map.ok());
  Handle note = hs[1];
  auto* h = reinterpret_cast<blk::RingHeader*>(ring_map.value);
  auto* sq = reinterpret_cast<blk::Request*>(ring_map.value + blk::kSqOffset);
  auto* cq = reinterpret_cast<blk::Completion*>(ring_map.value + blk::kCqOffset);
  print("virtio_blk: ready\n");

  auto port = port_create();
  EXPECT(port.ok());
  Handle port_h = static_cast<Handle>(port.value);
  constexpr uint64_t kKeyIrq = 1, kKeyKick = 2, kKeyKernel = 3;
  bool irq_armed = false, kick_armed = false, kernel_armed = false;
  uint32_t pending_sq = 0;  // запросы из sq, которым не хватило места в очереди

  for (;;) {
    // Новые запросы ядра.
    for (;;) {
      uint32_t head = __atomic_load_n(&h->sq_head, __ATOMIC_RELAXED);
      if (head == __atomic_load_n(&h->sq_tail, __ATOMIC_ACQUIRE)) break;
      uint32_t s = 0;
      while (s < kMaxInflight && d.slots[s].busy) ++s;
      if (s == kMaxInflight) {
        pending_sq = 1;
        break;
      }
      blk::Request r = sq[head % blk::kRingEntries];
      __atomic_store_n(&h->sq_head, head + 1, __ATOMIC_RELEASE);
      bool valid = (r.op == blk::kOpFlush && r.segment_count == 0) ||
                   ((r.op == blk::kOpRead || r.op == blk::kOpWrite) && r.segment_count >= 1 &&
                    r.segment_count <= blk::kMaxSegments);
      if (!valid || (r.op == blk::kOpFlush && (accepted & kFeatureFlush) == 0)) {
        uint32_t t = h->cq_tail;
        cq[t % blk::kRingEntries] = {r.id, static_cast<int32_t>(Status::InvalidArgs), 0};
        __atomic_store_n(&h->cq_tail, t + 1, __ATOMIC_RELEASE);
        continue;
      }
      submit(d, s, r);
    }
    // Готовые запросы устройства (§2.7.8).
    bool completed = false;
    fence();
    uint16_t used_idx = *reinterpret_cast<volatile uint16_t*>(d.used + 2);
    while (d.used_seen != used_idx) {
      volatile uint8_t* e = d.used + 4 + (d.used_seen % d.queue_size) * 8;
      uint32_t first = *reinterpret_cast<volatile uint32_t*>(e);
      ++d.used_seen;
      uint32_t s = first / kDescPerRequest;
      if (s >= kMaxInflight || !d.slots[s].busy) continue;
      uint8_t st = d.req_mem[s * 32 + 16];
      Status result = st == kBlkOk ? Status::Ok : st == kBlkUnsupp ? Status::NotSupported
                                                                   : Status::IoError;
      uint32_t t = h->cq_tail;
      cq[t % blk::kRingEntries] = {d.slots[s].id, static_cast<int32_t>(result), 0};
      __atomic_store_n(&h->cq_tail, t + 1, __ATOMIC_RELEASE);
      d.slots[s].busy = false;
      completed = true;
    }
    if (completed && __atomic_load_n(&h->kernel_wants_kick, __ATOMIC_ACQUIRE) != 0) {
      __atomic_store_n(&h->kernel_wants_kick, 0u, __ATOMIC_RELAXED);
      char none = 0;
      (void)channel_write(note, &none, 0, nullptr, 0);
    }
    if (pending_sq != 0 && completed) {
      pending_sq = 0;
      continue;  // место освободилось
    }

    // Спать: звонок ядра (с повторной проверкой sq), прерывание, закрытие канала.
    __atomic_store_n(&h->driver_wants_kick, 1u, __ATOMIC_SEQ_CST);
    if (pending_sq == 0 &&
        __atomic_load_n(&h->sq_head, __ATOMIC_RELAXED) != __atomic_load_n(&h->sq_tail, __ATOMIC_ACQUIRE)) {
      continue;
    }
    if (!irq_armed) irq_armed = object_bind(port_h, irq_h, kKeyIrq) == Status::Ok;
    if (!kick_armed) kick_armed = object_bind(port_h, note, kKeyKick) == Status::Ok;
    if (!kernel_armed) kernel_armed = object_bind(port_h, kernel, kKeyKernel) == Status::Ok;
    PortPacket pk;
    if (port_wait(port_h, &pk, kForever) != Status::Ok) continue;
    if (pk.key == kKeyIrq) {
      irq_armed = false;
      (void)syscall1(Number::InterruptAck, irq_h);
    } else if (pk.key == kKeyKick) {
      kick_armed = false;
      Handle none_h[1];
      while (channel_read(note, buf, sizeof buf, none_h, 1, 0).ok()) {
      }
    } else if (pk.key == kKeyKernel) {
      kernel_armed = false;
      if (pk.type == abi::syscall::kPacketPeerClosed) return 0;  // ядро просит уйти
    }
  }
}
