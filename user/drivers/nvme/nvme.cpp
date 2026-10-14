// Драйвер NVMe: docs/specs/nvme-base-2.1.pdf (регистры §3.1.4, очереди,
// Identify, Create I/O Queue), nvme-nvm-cmdset-1.1.pdf (Identify Namespace,
// Read/Write/Flush), nvme-pcie-1.4.pdf (doorbell §3.1.2, MSI-X).
// Одна пара очередей I/O, пространство имён 1, страница памяти 4 КиБ.
// С ядром — по протоколу docs/abi/block.md.
#include <abi/block.hpp>

#include "../../tests/check.hpp"

using namespace charm;
namespace blk = abi::block;

namespace {

// Регистры контроллера (Base §3.1.4).
constexpr uint64_t kRegCap = 0x00;
constexpr uint64_t kRegCc = 0x14;
constexpr uint64_t kRegCsts = 0x1c;
constexpr uint64_t kRegAqa = 0x24;
constexpr uint64_t kRegAsq = 0x28;
constexpr uint64_t kRegAcq = 0x30;
constexpr uint64_t kDoorbellBase = 0x1000;  // PCIe Transport §3.1.2

constexpr uint32_t kCcEn = 1u << 0;
constexpr uint32_t kCcIosqes = 6u << 16;  // 2^6 = 64 байта
constexpr uint32_t kCcIocqes = 4u << 20;  // 2^4 = 16 байт
constexpr uint32_t kCstsRdy = 1u << 0;
constexpr uint32_t kCstsCfs = 1u << 1;

// Admin (Base рис. 141) и NVM (NVM Command Set рис. 21).
constexpr uint8_t kAdminCreateSq = 0x01;
constexpr uint8_t kAdminCreateCq = 0x05;
constexpr uint8_t kAdminIdentify = 0x06;
constexpr uint8_t kNvmFlush = 0x00;
constexpr uint8_t kNvmWrite = 0x01;
constexpr uint8_t kNvmRead = 0x02;
constexpr uint32_t kCnsNamespace = 0x00;
constexpr uint32_t kCnsController = 0x01;

constexpr uint32_t kAdminEntries = 16;
constexpr uint32_t kIoEntries = 64;
constexpr uint32_t kSlots = 16;  // запросов в работе одновременно
constexpr uint64_t kPage = 4096;

// Base рис. 92: команда (64 байта).
struct Sqe {
  uint32_t cdw0;  // 7:0 опкод, 31:16 CID
  uint32_t nsid;
  uint32_t cdw2, cdw3;
  uint64_t mptr;
  uint64_t prp1;
  uint64_t prp2;
  uint32_t cdw10, cdw11, cdw12, cdw13, cdw14, cdw15;
};
static_assert(sizeof(Sqe) == 64);
static_assert(__builtin_offsetof(Sqe, prp1) == 24);
static_assert(__builtin_offsetof(Sqe, cdw10) == 40);

// Base рис. 96: ответ (16 байт).
struct Cqe {
  uint32_t dw0;
  uint32_t dw1;
  uint16_t sq_head;
  uint16_t sq_id;
  uint16_t cid;
  uint16_t status;  // бит 0 — фаза, 15:1 — статус
};
static_assert(sizeof(Cqe) == 16);

struct Queue {
  volatile Sqe* sq;
  volatile Cqe* cq;
  uint64_t sq_iova, cq_iova;
  uint32_t entries;
  uint32_t sq_tail;
  uint32_t cq_head;
  uint16_t phase;
  uint64_t sq_doorbell, cq_doorbell;
};

struct Slot {
  bool busy;
  uint64_t id;
};

uint64_t g_regs;
uint32_t g_stride;

void fence() { __atomic_thread_fence(__ATOMIC_SEQ_CST); }

uint32_t r32(uint64_t off) { return *reinterpret_cast<volatile uint32_t*>(g_regs + off); }
uint64_t r64(uint64_t off) { return r32(off) | uint64_t{r32(off + 4)} << 32; }
void w32(uint64_t off, uint32_t v) { *reinterpret_cast<volatile uint32_t*>(g_regs + off) = v; }
void w64(uint64_t off, uint64_t v) {
  w32(off, static_cast<uint32_t>(v));
  w32(off + 4, static_cast<uint32_t>(v >> 32));
}

bool alloc_page(Handle dev, uint64_t* iova, uint64_t* addr) {
  auto v = dma_alloc(dev, kPage, 0, iova);
  if (!v.ok()) return false;
  auto m = vmo_map(static_cast<Handle>(v.value), rights::kRead | rights::kWrite);
  if (!m.ok()) return false;
  *addr = m.value;
  return true;
}

bool make_queue(Handle dev, Queue& q, uint32_t qid, uint32_t entries) {
  uint64_t a;
  if (!alloc_page(dev, &q.sq_iova, &a)) return false;
  q.sq = reinterpret_cast<volatile Sqe*>(a);
  if (!alloc_page(dev, &q.cq_iova, &a)) return false;
  q.cq = reinterpret_cast<volatile Cqe*>(a);
  q.entries = entries;
  q.sq_tail = q.cq_head = 0;
  q.phase = 1;
  q.sq_doorbell = kDoorbellBase + (2 * qid) * g_stride;
  q.cq_doorbell = kDoorbellBase + (2 * qid + 1) * g_stride;
  return true;
}

void push(Queue& q, const Sqe& c) {
  volatile Sqe& e = q.sq[q.sq_tail];
  e.cdw0 = c.cdw0;
  e.nsid = c.nsid;
  e.cdw2 = 0;
  e.cdw3 = 0;
  e.mptr = 0;
  e.prp1 = c.prp1;
  e.prp2 = c.prp2;
  e.cdw10 = c.cdw10;
  e.cdw11 = c.cdw11;
  e.cdw12 = c.cdw12;
  e.cdw13 = c.cdw13;
  e.cdw14 = c.cdw14;
  e.cdw15 = c.cdw15;
  q.sq_tail = (q.sq_tail + 1) % q.entries;
  fence();
  w32(q.sq_doorbell, q.sq_tail);
}

// Следующий ответ, если есть (по биту фазы, Base §4.6).
bool pop(Queue& q, Cqe* out) {
  fence();
  volatile Cqe& e = q.cq[q.cq_head];
  if ((e.status & 1) != q.phase) return false;
  out->dw0 = e.dw0;
  out->cid = e.cid;
  out->status = e.status;
  q.cq_head = (q.cq_head + 1) % q.entries;
  if (q.cq_head == 0) q.phase ^= 1;
  w32(q.cq_doorbell, q.cq_head);
  return true;
}

bool status_ok(const Cqe& c) { return ((c.status >> 1) & 0x7ff) == 0; }  // SCT и SC

// Команда администратора с ожиданием (опросом).
bool admin(Queue& a, Sqe c, Cqe* out) {
  static uint16_t cid = 0;
  c.cdw0 |= uint32_t{++cid} << 16;
  push(a, c);
  uint64_t deadline = now_ns() + 5'000'000'000;
  while (!pop(a, out)) {
    if (now_ns() > deadline) return false;
    yield();
  }
  return out->cid == cid && status_ok(*out);
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
  EXPECT(info.bar_kind[0] == abi::syscall::kBarMem64 || info.bar_kind[0] == abi::syscall::kBarMem32);
  auto bar = syscall2(Number::DeviceMapBar, dev, 0);
  EXPECT(bar.ok());
  auto map = vmo_map(static_cast<Handle>(bar.value), rights::kRead | rights::kWrite);
  EXPECT(map.ok());
  g_regs = map.value;

  uint64_t cap = r64(kRegCap);
  g_stride = 4u << ((cap >> 32) & 0xf);                     // DSTRD
  uint64_t timeout_ns = ((cap >> 24) & 0xff) * 500'000'000;  // TO, по 500 мс
  uint32_t mqes = static_cast<uint32_t>(cap & 0xffff) + 1;
  EXPECT(((cap >> 48) & 0xf) == 0);   // MPSMIN = 4 КиБ
  EXPECT(((cap >> 37) & 1) != 0);     // CSS: NVM Command Set
  EXPECT(mqes >= kIoEntries);

  // Сброс и настройка admin-очередей (Base §3.5.1).
  w32(kRegCc, 0);
  uint64_t deadline = now_ns() + timeout_ns + 1'000'000'000;
  while ((r32(kRegCsts) & kCstsRdy) != 0) {
    EXPECT(now_ns() < deadline);
    yield();
  }
  Queue aq, ioq;
  EXPECT(make_queue(dev, aq, 0, kAdminEntries));
  w32(kRegAqa, (kAdminEntries - 1) << 16 | (kAdminEntries - 1));
  w64(kRegAsq, aq.sq_iova);
  w64(kRegAcq, aq.cq_iova);
  w32(kRegCc, kCcEn | kCcIosqes | kCcIocqes);  // CSS = NVM, MPS = 4 КиБ, AMS = round robin
  deadline = now_ns() + timeout_ns + 1'000'000'000;
  while ((r32(kRegCsts) & kCstsRdy) == 0) {
    EXPECT((r32(kRegCsts) & kCstsCfs) == 0);
    EXPECT(now_ns() < deadline);
    yield();
  }

  // Identify: контроллер (MDTS) и пространство имён 1.
  uint64_t id_iova, id_addr;
  EXPECT(alloc_page(dev, &id_iova, &id_addr));
  auto* id = reinterpret_cast<volatile uint8_t*>(id_addr);
  Cqe c;
  Sqe cmd = {};
  cmd.cdw0 = kAdminIdentify;
  cmd.prp1 = id_iova;
  cmd.cdw10 = kCnsController;
  EXPECT(admin(aq, cmd, &c));
  uint8_t mdts = id[77];
  // 6 страниц на запрос (block.md §5.3) должны помещаться в MDTS.
  EXPECT(mdts == 0 || (1u << mdts) >= blk::kMaxSegments);
  cmd = {};
  cmd.cdw0 = kAdminIdentify;
  cmd.nsid = 1;
  cmd.prp1 = id_iova;
  cmd.cdw10 = kCnsNamespace;
  EXPECT(admin(aq, cmd, &c));
  uint64_t nsze = 0;
  for (int i = 7; i >= 0; --i) nsze = nsze << 8 | id[i];
  uint32_t format = id[26] & 0xf;  // FLBAS
  uint32_t lbaf = static_cast<uint32_t>(id[128 + 4 * format]) | uint32_t{id[129 + 4 * format]} << 8 |
                  uint32_t{id[130 + 4 * format]} << 16 | uint32_t{id[131 + 4 * format]} << 24;
  uint32_t lbads = (lbaf >> 16) & 0xff;
  EXPECT(nsze != 0 && (lbads == 9 || lbads == 12));
  EXPECT((lbaf & 0xffff) == 0);  // без метаданных
  uint32_t block_size = 1u << lbads;

  // Пара очередей I/O 1 с прерыванием MSI-X 0 (Base §5.2.1, §5.2.2).
  auto irq = syscall2(Number::DeviceIrq, dev, 0);
  EXPECT(irq.ok());
  Handle irq_h = static_cast<Handle>(irq.value);
  EXPECT(make_queue(dev, ioq, 1, kIoEntries));
  cmd = {};
  cmd.cdw0 = kAdminCreateCq;
  cmd.prp1 = ioq.cq_iova;
  cmd.cdw10 = (kIoEntries - 1) << 16 | 1;
  cmd.cdw11 = 0u << 16 | 1u << 1 | 1u;  // IV 0, IEN, PC
  EXPECT(admin(aq, cmd, &c));
  cmd = {};
  cmd.cdw0 = kAdminCreateSq;
  cmd.prp1 = ioq.sq_iova;
  cmd.cdw10 = (kIoEntries - 1) << 16 | 1;
  cmd.cdw11 = 1u << 16 | 1u;  // CQID 1, PC
  EXPECT(admin(aq, cmd, &c));

  // Списки PRP: по 64 байта на слот в одной странице.
  uint64_t prp_iova, prp_addr;
  EXPECT(alloc_page(dev, &prp_iova, &prp_addr));
  auto* prp = reinterpret_cast<volatile uint64_t*>(prp_addr);

  blk::BlockHello hello = {};
  for (int i = 0; i < 4; ++i) hello.magic[i] = blk::kHelloMagic[i];
  hello.block_size = block_size;
  hello.block_count = nsze;
  hello.max_blocks = static_cast<uint32_t>(blk::kMaxSegments * kPage / block_size);
  hello.flags = blk::kHelloFlush;
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
  print("nvme: ready\n");

  auto port = port_create();
  EXPECT(port.ok());
  Handle port_h = static_cast<Handle>(port.value);
  constexpr uint64_t kKeyIrq = 1, kKeyKick = 2, kKeyKernel = 3;
  bool irq_armed = false, kick_armed = false, kernel_armed = false;
  Slot slots[kSlots] = {};

  auto complete = [&](uint64_t rid, Status s) {
    uint32_t t = h->cq_tail;
    cq[t % blk::kRingEntries] = {rid, static_cast<int32_t>(s), 0};
    __atomic_store_n(&h->cq_tail, t + 1, __ATOMIC_RELEASE);
  };

  for (;;) {
    bool full = false;
    for (;;) {
      uint32_t head = __atomic_load_n(&h->sq_head, __ATOMIC_RELAXED);
      if (head == __atomic_load_n(&h->sq_tail, __ATOMIC_ACQUIRE)) break;
      uint32_t s = 0;
      while (s < kSlots && slots[s].busy) ++s;
      if (s == kSlots) {
        full = true;
        break;
      }
      blk::Request r = sq[head % blk::kRingEntries];
      __atomic_store_n(&h->sq_head, head + 1, __ATOMIC_RELEASE);
      bool rw = r.op == blk::kOpRead || r.op == blk::kOpWrite;
      bool valid = (r.op == blk::kOpFlush && r.segment_count == 0) ||
                   (rw && r.segment_count >= 1 && r.segment_count <= blk::kMaxSegments && r.blocks >= 1);
      for (uint32_t k = 0; valid && rw && k < r.segment_count; ++k) {
        // Каждый сегмент — целая страница (PRP: смещение 0, длина 4 КиБ).
        valid = r.segments[k].bytes == kPage && r.segments[k].iova % kPage == 0;
      }
      if (!valid) {
        complete(r.id, Status::InvalidArgs);
        continue;
      }
      Sqe io = {};
      io.cdw0 = uint32_t{s} << 16;
      io.nsid = 1;
      if (r.op == blk::kOpFlush) {
        io.cdw0 |= kNvmFlush;
      } else {
        io.cdw0 |= r.op == blk::kOpRead ? kNvmRead : kNvmWrite;
        io.prp1 = r.segments[0].iova;
        if (r.segment_count == 2) {
          io.prp2 = r.segments[1].iova;
        } else if (r.segment_count > 2) {
          for (uint32_t k = 1; k < r.segment_count; ++k) prp[s * 8 + k - 1] = r.segments[k].iova;
          io.prp2 = prp_iova + s * 64;
        }
        io.cdw10 = static_cast<uint32_t>(r.lba);
        io.cdw11 = static_cast<uint32_t>(r.lba >> 32);
        io.cdw12 = r.blocks - 1;  // 0's based
      }
      slots[s] = {true, r.id};
      push(ioq, io);
    }

    bool done_any = false;
    Cqe e;
    while (pop(ioq, &e)) {
      if (e.cid >= kSlots || !slots[e.cid].busy) continue;
      complete(slots[e.cid].id, status_ok(e) ? Status::Ok : Status::IoError);
      slots[e.cid].busy = false;
      done_any = true;
    }
    if (done_any && __atomic_load_n(&h->kernel_wants_kick, __ATOMIC_ACQUIRE) != 0) {
      __atomic_store_n(&h->kernel_wants_kick, 0u, __ATOMIC_RELAXED);
      char none = 0;
      (void)channel_write(note, &none, 0, nullptr, 0);
    }
    if (full && done_any) continue;

    __atomic_store_n(&h->driver_wants_kick, 1u, __ATOMIC_SEQ_CST);
    if (!full && __atomic_load_n(&h->sq_head, __ATOMIC_RELAXED) !=
                     __atomic_load_n(&h->sq_tail, __ATOMIC_ACQUIRE)) {
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
      if (pk.type == abi::syscall::kPacketPeerClosed) return 0;
    }
  }
}
