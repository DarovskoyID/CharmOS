// Драйвер SATA AHCI: docs/specs/ahci-1.3.1.pdf (регистры §3, структуры §4,
// инициализация §10.1.2), команды ATA — acs-3-r5.pdf (IDENTIFY DEVICE ECh,
// READ/WRITE DMA EXT 25h/35h, FLUSH CACHE EXT EAh). Формат H2D Register FIS
// описан только в закрытом Serial ATA Revision: ИСТОЧНИК ВТОРИЧНЫЙ —
// docs/specs/sata-ahci-summary.md §5, проверен тестом m6/ahci.
// Первый порт с подключённым диском, одна команда в работе (слот 0).
// С ядром — по протоколу docs/abi/block.md.
#include <abi/block.hpp>

#include "../../tests/check.hpp"

using namespace charm;
namespace blk = abi::block;

namespace {

// AHCI §3.1: общие регистры.
constexpr uint64_t kCap = 0x00;
constexpr uint64_t kGhc = 0x04;
constexpr uint64_t kIs = 0x08;
constexpr uint64_t kPi = 0x0c;
constexpr uint32_t kGhcAe = 1u << 31;
constexpr uint32_t kGhcIe = 1u << 1;
constexpr uint32_t kCapS64a = 1u << 31;

// AHCI §3.3: регистры порта, база 100h + n * 80h.
constexpr uint64_t kPxClb = 0x00;
constexpr uint64_t kPxFb = 0x08;
constexpr uint64_t kPxIs = 0x10;
constexpr uint64_t kPxIe = 0x14;
constexpr uint64_t kPxCmd = 0x18;
constexpr uint64_t kPxTfd = 0x20;
constexpr uint64_t kPxSsts = 0x28;
constexpr uint64_t kPxSerr = 0x30;
constexpr uint64_t kPxCi = 0x38;
constexpr uint32_t kCmdSt = 1u << 0;
constexpr uint32_t kCmdFre = 1u << 4;
constexpr uint32_t kCmdFr = 1u << 14;
constexpr uint32_t kCmdCr = 1u << 15;
constexpr uint32_t kIsDhrs = 1u << 0;
constexpr uint32_t kIsFatal = (1u << 30) | (1u << 29) | (1u << 28) | (1u << 27);  // TFES HBFS HBDS IFS
constexpr uint32_t kTfdBsy = 1u << 7;
constexpr uint32_t kTfdDrq = 1u << 3;
constexpr uint32_t kTfdErr = 1u << 0;

// ACS-3 §7.12, 7.22, 7.59, 7.11.
constexpr uint8_t kAtaIdentify = 0xec;
constexpr uint8_t kAtaReadDmaExt = 0x25;
constexpr uint8_t kAtaWriteDmaExt = 0x35;
constexpr uint8_t kAtaFlushExt = 0xea;

// AHCI §4.2.2: заголовок команды (32 байта).
struct CmdHeader {
  uint32_t dw0;  // 4:0 CFL, 6 W, 31:16 PRDTL
  uint32_t prdbc;
  uint64_t ctba;
  uint32_t reserved[4];
};
static_assert(sizeof(CmdHeader) == 32);

// AHCI §4.2.3: элемент PRDT (16 байт).
struct Prd {
  uint64_t dba;
  uint32_t reserved;
  uint32_t dbc;  // 21:0 — длина − 1, 31 — прерывание
};
static_assert(sizeof(Prd) == 16);
constexpr uint64_t kPrdtOffset = 0x80;

uint64_t g_regs;
uint64_t g_port;

uint32_t r32(uint64_t off) { return *reinterpret_cast<volatile uint32_t*>(off); }
void w32(uint64_t off, uint32_t v) { *reinterpret_cast<volatile uint32_t*>(off) = v; }
void fence() { __atomic_thread_fence(__ATOMIC_SEQ_CST); }

bool wait_clear(uint64_t reg, uint32_t bits, uint64_t ns) {
  uint64_t deadline = now_ns() + ns;
  while ((r32(reg) & bits) != 0) {
    if (now_ns() > deadline) return false;
    yield();
  }
  return true;
}

volatile CmdHeader* g_list;
volatile uint8_t* g_table;
uint64_t g_table_iova;

// Выдаёт команду в слот 0 и ждёт её. Данные — сегменты (адрес, длина).
bool run(uint8_t command, uint64_t lba, uint32_t count, bool write, const blk::Segment* segs,
         uint32_t nseg, Handle irq, Handle port_h) {
  if (!wait_clear(g_port + kPxTfd, kTfdBsy | kTfdDrq, 1'000'000'000)) return false;
  // H2D Register FIS (источник вторичный: sata-ahci-summary.md §5).
  volatile uint8_t* f = g_table;
  for (int i = 0; i < 64; ++i) f[i] = 0;
  f[0] = 0x27;
  f[1] = 0x80;  // C: команда
  f[2] = command;
  f[4] = static_cast<uint8_t>(lba);
  f[5] = static_cast<uint8_t>(lba >> 8);
  f[6] = static_cast<uint8_t>(lba >> 16);
  f[7] = 0x40;  // устройство: режим LBA
  f[8] = static_cast<uint8_t>(lba >> 24);
  f[9] = static_cast<uint8_t>(lba >> 32);
  f[10] = static_cast<uint8_t>(lba >> 40);
  f[12] = static_cast<uint8_t>(count);
  f[13] = static_cast<uint8_t>(count >> 8);
  auto* prd = reinterpret_cast<volatile Prd*>(g_table + kPrdtOffset);
  for (uint32_t i = 0; i < nseg; ++i) {
    prd[i].dba = segs[i].iova;
    prd[i].reserved = 0;
    prd[i].dbc = (segs[i].bytes - 1) | (i + 1 == nseg ? 1u << 31 : 0);
  }
  g_list[0].dw0 = 5u | (write ? 1u << 6 : 0) | (nseg << 16);  // CFL = 5 DW
  g_list[0].prdbc = 0;
  g_list[0].ctba = g_table_iova;
  fence();
  w32(g_port + kPxIs, ~0u);
  w32(g_port + kPxCi, 1);
  uint64_t deadline = now_ns() + 10'000'000'000;
  while ((r32(g_port + kPxCi) & 1) != 0) {
    if ((r32(g_port + kPxIs) & kIsFatal) != 0 || now_ns() > deadline) return false;
    // Ждём прерывания с коротким сроком: QEMU-модель и железо могут прислать
    // его раньше, чем мы привяжемся.
    (void)object_bind(port_h, irq, 1);
    PortPacket pk;
    if (port_wait(port_h, &pk, now_ns() + 10'000'000) == Status::Ok) {
      (void)syscall1(Number::InterruptAck, irq);
    }
  }
  w32(g_port + kPxIs, ~0u);
  return (r32(g_port + kPxTfd) & kTfdErr) == 0;
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
  if ((info.flags & abi::syscall::kDeviceDma) == 0) {
    print("ahci: no DMA (no IOMMU), not starting\n");
    return 3;
  }
  EXPECT(info.irq_count >= 1 && info.bar_kind[5] != abi::syscall::kBarAbsent);
  auto bar = syscall2(Number::DeviceMapBar, dev, 5);  // ABAR (AHCI §2.1.11)
  EXPECT(bar.ok());
  auto map = vmo_map(static_cast<Handle>(bar.value), rights::kRead | rights::kWrite);
  EXPECT(map.ok());
  g_regs = map.value;

  w32(g_regs + kGhc, r32(g_regs + kGhc) | kGhcAe);
  uint32_t cap = r32(g_regs + kCap);
  uint32_t pi = r32(g_regs + kPi);
  // Первый реализованный порт с устройством: DET = 3, IPM = 1 (AHCI §3.3.10).
  int port = -1;
  for (int p = 0; p < 32 && port < 0; ++p) {
    if ((pi & (1u << p)) == 0) continue;
    uint32_t ssts = r32(g_regs + 0x100 + uint64_t(p) * 0x80 + kPxSsts);
    if ((ssts & 0xf) == 3 && ((ssts >> 8) & 0xf) == 1) port = p;
  }
  if (port < 0) {
    print("ahci: no disk\n");
    return 4;
  }
  g_port = g_regs + 0x100 + uint64_t(port) * 0x80;

  // Остановить порт, задать список команд и область FIS (AHCI §10.1.2).
  w32(g_port + kPxCmd, r32(g_port + kPxCmd) & ~kCmdSt);
  EXPECT(wait_clear(g_port + kPxCmd, kCmdCr, 500'000'000));
  w32(g_port + kPxCmd, r32(g_port + kPxCmd) & ~kCmdFre);
  EXPECT(wait_clear(g_port + kPxCmd, kCmdFr, 500'000'000));
  uint64_t mem_iova;
  auto mem = dma_alloc(dev, 4096, 0, &mem_iova);
  EXPECT(mem.ok());
  EXPECT((cap & kCapS64a) != 0 || mem_iova < (1ull << 32));
  auto mmap = vmo_map(static_cast<Handle>(mem.value), rights::kRead | rights::kWrite);
  EXPECT(mmap.ok());
  // Страница: 0 — список команд (1 КиБ), 1024 — принятые FIS (256 Б),
  // 2048 — таблица команды (выравнивание 128 Б).
  g_list = reinterpret_cast<volatile CmdHeader*>(mmap.value);
  g_table = reinterpret_cast<volatile uint8_t*>(mmap.value + 2048);
  g_table_iova = mem_iova + 2048;
  w32(g_port + kPxClb, static_cast<uint32_t>(mem_iova));
  w32(g_port + kPxClb + 4, static_cast<uint32_t>(mem_iova >> 32));
  w32(g_port + kPxFb, static_cast<uint32_t>(mem_iova + 1024));
  w32(g_port + kPxFb + 4, static_cast<uint32_t>((mem_iova + 1024) >> 32));
  w32(g_port + kPxSerr, r32(g_port + kPxSerr));
  w32(g_port + kPxIs, ~0u);
  w32(g_port + kPxCmd, r32(g_port + kPxCmd) | kCmdFre);
  w32(g_port + kPxCmd, r32(g_port + kPxCmd) | kCmdSt);
  w32(g_port + kPxIe, kIsDhrs | kIsFatal);
  w32(g_regs + kIs, ~0u);
  w32(g_regs + kGhc, r32(g_regs + kGhc) | kGhcIe);

  auto irq = syscall2(Number::DeviceIrq, dev, 0);
  EXPECT(irq.ok());
  Handle irq_h = static_cast<Handle>(irq.value);
  auto port_c = port_create();
  EXPECT(port_c.ok());
  Handle port_h = static_cast<Handle>(port_c.value);

  // IDENTIFY DEVICE (ACS-3 §7.12): LBA48 (слово 83 бит 10), ёмкость (100..103),
  // сектор 512 байт (слово 106 бит 12 сброшен).
  uint64_t id_iova;
  auto idm = dma_alloc(dev, 4096, 0, &id_iova);
  EXPECT(idm.ok());
  auto idmap = vmo_map(static_cast<Handle>(idm.value), rights::kRead | rights::kWrite);
  EXPECT(idmap.ok());
  blk::Segment seg = {id_iova, 512, 0};
  EXPECT(run(kAtaIdentify, 0, 0, false, &seg, 1, irq_h, port_h));
  auto* id = reinterpret_cast<volatile uint16_t*>(idmap.value);
  EXPECT((id[83] & (1u << 10)) != 0);
  uint16_t w106 = id[106];
  EXPECT(!((w106 & 0xc000) == 0x4000 && (w106 & (1u << 12)) != 0));  // только 512-байтовые сектора
  uint64_t sectors = uint64_t{id[100]} | uint64_t{id[101]} << 16 | uint64_t{id[102]} << 32 |
                     uint64_t{id[103]} << 48;
  EXPECT(sectors != 0);

  blk::BlockHello hello = {};
  for (int i = 0; i < 4; ++i) hello.magic[i] = blk::kHelloMagic[i];
  hello.block_size = 512;
  hello.block_count = sectors;
  hello.max_blocks = blk::kMaxSegments * 8;
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
  print("ahci: ready\n");

  for (;;) {
    uint32_t head = __atomic_load_n(&h->sq_head, __ATOMIC_RELAXED);
    if (head != __atomic_load_n(&h->sq_tail, __ATOMIC_ACQUIRE)) {
      blk::Request r = sq[head % blk::kRingEntries];
      __atomic_store_n(&h->sq_head, head + 1, __ATOMIC_RELEASE);
      Status s;
      if (r.op == blk::kOpFlush && r.segment_count == 0) {
        s = run(kAtaFlushExt, 0, 0, false, nullptr, 0, irq_h, port_h) ? Status::Ok : Status::IoError;
      } else if ((r.op == blk::kOpRead || r.op == blk::kOpWrite) && r.segment_count >= 1 &&
                 r.segment_count <= blk::kMaxSegments && r.blocks >= 1 && r.blocks <= 65536) {
        bool write = r.op == blk::kOpWrite;
        s = run(write ? kAtaWriteDmaExt : kAtaReadDmaExt, r.lba, r.blocks == 65536 ? 0 : r.blocks,
                write, r.segments, r.segment_count, irq_h, port_h)
                ? Status::Ok
                : Status::IoError;
      } else {
        s = Status::InvalidArgs;
      }
      uint32_t t = h->cq_tail;
      cq[t % blk::kRingEntries] = {r.id, static_cast<int32_t>(s), 0};
      __atomic_store_n(&h->cq_tail, t + 1, __ATOMIC_RELEASE);
      if (__atomic_load_n(&h->kernel_wants_kick, __ATOMIC_ACQUIRE) != 0) {
        __atomic_store_n(&h->kernel_wants_kick, 0u, __ATOMIC_RELAXED);
        char none = 0;
        (void)channel_write(note, &none, 0, nullptr, 0);
      }
      continue;
    }
    __atomic_store_n(&h->driver_wants_kick, 1u, __ATOMIC_SEQ_CST);
    if (__atomic_load_n(&h->sq_head, __ATOMIC_RELAXED) != __atomic_load_n(&h->sq_tail, __ATOMIC_ACQUIRE)) {
      continue;
    }
    // Звонок ядра или закрытие стартового канала.
    (void)object_bind(port_h, note, 2);
    (void)object_bind(port_h, kernel, 3);
    PortPacket pk;
    if (port_wait(port_h, &pk, kForever) != Status::Ok) continue;
    if (pk.key == 2) {
      Handle none_h[1];
      while (channel_read(note, buf, sizeof buf, none_h, 1, 0).ok()) {
      }
    } else if (pk.key == 3 && pk.type == abi::syscall::kPacketPeerClosed) {
      return 0;
    } else if (pk.key == 1) {
      (void)syscall1(Number::InterruptAck, irq_h);  // запоздалое прерывание прошлой команды
    }
  }
}
