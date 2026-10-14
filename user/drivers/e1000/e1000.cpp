// Драйвер Intel 8254x (e1000): docs/specs/intel-8254x-sdm.pdf — регистры
// §13 (CTRL, STATUS, RCTL, TCTL, TIPG, RDBA/RDLEN/RDH/RDT, TDBA/…, RAL/RAH,
// MTA), дескрипторы §3.2 (табл. 3-1) и §3.3 (табл. 3-8, 3-10).
// Прерывание у 8254x — только INTx (нет MSI/MSI-X), а маршрутизация INTx
// требует _PRT из ACPI AML (ROADMAP M9): пока драйвер работает опросом.
// С ядром — по протоколу docs/abi/net.md.
#include <abi/net.hpp>

#include "../../tests/check.hpp"

using namespace charm;
namespace abin = abi::net;

namespace {

constexpr uint64_t kCtrl = 0x0000, kStatus = 0x0008, kImc = 0x00d8;
constexpr uint64_t kRctl = 0x0100, kTctl = 0x0400, kTipg = 0x0410;
constexpr uint64_t kRdbal = 0x2800, kRdbah = 0x2804, kRdlen = 0x2808, kRdh = 0x2810, kRdt = 0x2818;
constexpr uint64_t kTdbal = 0x3800, kTdbah = 0x3804, kTdlen = 0x3808, kTdh = 0x3810, kTdt = 0x3818;
constexpr uint64_t kMta = 0x5200, kRal = 0x5400, kRah = 0x5404;
constexpr uint32_t kCtrlSlu = 1u << 6, kCtrlRst = 1u << 26;
constexpr uint32_t kStatusLu = 1u << 1;
constexpr uint32_t kRctlEn = 1u << 1, kRctlBam = 1u << 15, kRctlSecrc = 1u << 26;  // BSIZE 00 = 2048
constexpr uint32_t kTctlEn = 1u << 1, kTctlPsp = 1u << 3;
constexpr uint32_t kTctlCt = 0x10u << 4, kTctlCold = 0x40u << 12;  // рекомендованные значения (§13)
constexpr uint32_t kTipgValue = 10u | 8u << 10 | 6u << 20;          // IPGT/IPGR1/IPGR2 для 8254x
constexpr uint8_t kRxDd = 1u << 0;
constexpr uint8_t kTxEop = 1u << 0, kTxIfcs = 1u << 1, kTxRs = 1u << 3, kTxDd = 1u << 0;

struct RxDesc {
  uint64_t addr;
  uint16_t length;
  uint16_t checksum;
  uint8_t status;
  uint8_t errors;
  uint16_t special;
};
static_assert(sizeof(RxDesc) == 16);
struct TxDesc {
  uint64_t addr;
  uint16_t length;
  uint8_t cso;
  uint8_t cmd;
  uint8_t status;
  uint8_t css;
  uint16_t special;
};
static_assert(sizeof(TxDesc) == 16);

constexpr uint32_t kN = 64;  // дескрипторов в каждом кольце (кратно 8, §13)
constexpr uint32_t kBufferSize = 2048;
constexpr uint32_t kBuffers = 256;

uint64_t g_regs;
uint32_t r32(uint64_t o) { return *reinterpret_cast<volatile uint32_t*>(g_regs + o); }
void w32(uint64_t o, uint32_t v) { *reinterpret_cast<volatile uint32_t*>(g_regs + o) = v; }
void fence() { __atomic_thread_fence(__ATOMIC_SEQ_CST); }

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
  auto bar = syscall2(Number::DeviceMapBar, dev, 0);
  EXPECT(bar.ok());
  auto map = vmo_map(static_cast<Handle>(bar.value), rights::kRead | rights::kWrite);
  EXPECT(map.ok());
  g_regs = map.value;

  // Сброс (CTRL.RST, самоочищается), прерывания маскированы, линия поднята.
  w32(kImc, ~0u);
  w32(kCtrl, r32(kCtrl) | kCtrlRst);
  uint64_t deadline = now_ns() + 1'000'000'000;
  while ((r32(kCtrl) & kCtrlRst) != 0) {
    EXPECT(now_ns() < deadline);
    yield();
  }
  w32(kImc, ~0u);
  w32(kCtrl, r32(kCtrl) | kCtrlSlu);
  for (uint32_t i = 0; i < 128; ++i) w32(kMta + i * 4, 0);
  // MAC — из RAL0/RAH0 (их загружает EEPROM при сбросе).
  uint32_t ral = r32(kRal), rah = r32(kRah);
  uint8_t mac[6] = {static_cast<uint8_t>(ral), static_cast<uint8_t>(ral >> 8), static_cast<uint8_t>(ral >> 16),
                    static_cast<uint8_t>(ral >> 24), static_cast<uint8_t>(rah), static_cast<uint8_t>(rah >> 8)};
  EXPECT((rah & (1u << 31)) != 0);  // AV: адрес действителен

  uint64_t pool_iova, ring_iova;
  auto pool = dma_alloc(dev, uint64_t{kBuffers} * kBufferSize, abi::syscall::kDmaContiguous, &pool_iova);
  EXPECT(pool.ok());
  auto rings_dma = dma_alloc(dev, 4096, 0, &ring_iova);
  EXPECT(rings_dma.ok());
  auto rmap = vmo_map(static_cast<Handle>(rings_dma.value), rights::kRead | rights::kWrite);
  EXPECT(rmap.ok());
  auto* rx = reinterpret_cast<volatile RxDesc*>(rmap.value);
  auto* tx = reinterpret_cast<volatile TxDesc*>(rmap.value + 2048);
  auto kr = vmo_create(abin::kRingBytes);
  EXPECT(kr.ok());
  auto krmap = vmo_map(static_cast<Handle>(kr.value), rights::kRead | rights::kWrite);
  EXPECT(krmap.ok());

  // Кольца устройства: пустые; RDT/TDT двигаем по мере появления буферов.
  w32(kRdbal, static_cast<uint32_t>(ring_iova));
  w32(kRdbah, static_cast<uint32_t>(ring_iova >> 32));
  w32(kRdlen, kN * sizeof(RxDesc));
  w32(kRdh, 0);
  w32(kRdt, 0);
  w32(kTdbal, static_cast<uint32_t>(ring_iova + 2048));
  w32(kTdbah, static_cast<uint32_t>((ring_iova + 2048) >> 32));
  w32(kTdlen, kN * sizeof(TxDesc));
  w32(kTdh, 0);
  w32(kTdt, 0);
  w32(kTipg, kTipgValue);
  w32(kTctl, kTctlEn | kTctlPsp | kTctlCt | kTctlCold);
  w32(kRctl, kRctlEn | kRctlBam | kRctlSecrc);

  abin::NetHello hello = {};
  for (int i = 0; i < 4; ++i) hello.magic[i] = abin::kHelloMagic[i];
  for (int i = 0; i < 6; ++i) hello.mac[i] = mac[i];
  hello.mtu = 1500;
  hello.buffer_size = kBufferSize;
  hello.buffer_count = kBuffers;
  Handle give[2] = {static_cast<Handle>(pool.value), static_cast<Handle>(kr.value)};
  EXPECT(channel_write(kernel, &hello, sizeof hello, give, 2) == Status::Ok);
  EXPECT(read_msg(kernel, buf, sizeof buf, hs, 2, &n, &nh) == Status::Ok);
  if (same(buf, n, "reject")) return 2;
  EXPECT(same(buf, n, "ok") && nh == 1);
  Handle note = hs[0];
  auto* h = reinterpret_cast<abin::NetRingHeader*>(krmap.value);
  auto slot = [&](uint64_t base, uint32_t i) -> abin::Slot* {
    return reinterpret_cast<abin::Slot*>(krmap.value + base) + i % abin::kRingEntries;
  };
  print("e1000: ready\n");

  uint32_t rx_buf[kN], tx_buf[kN];
  uint32_t rx_tail = 0, rx_next = 0, rx_used = 0;  // устройство: [rx_next, rx_tail)
  uint32_t tx_tail = 0, tx_clean = 0, tx_used = 0;
  auto port = port_create();
  EXPECT(port.ok());
  Handle port_h = static_cast<Handle>(port.value);
  bool kick_armed = false, kernel_armed = false;

  for (;;) {
    __atomic_store_n(&h->link_up, (r32(kStatus) & kStatusLu) != 0 ? 1u : 0u, __ATOMIC_RELEASE);
    bool any = false;
    // Пустые буферы ядра → кольцо приёма (один дескриптор оставляем пустым, §3.2.6).
    while (rx_used < kN - 1 && h->rx_free_head != __atomic_load_n(&h->rx_free_tail, __ATOMIC_ACQUIRE)) {
      abin::Slot s = *slot(abin::kRxFreeOffset, h->rx_free_head);
      __atomic_store_n(&h->rx_free_head, h->rx_free_head + 1, __ATOMIC_RELEASE);
      if (s.buffer >= kBuffers) continue;
      rx[rx_tail].addr = pool_iova + uint64_t(s.buffer) * kBufferSize;
      rx[rx_tail].status = 0;
      rx_buf[rx_tail] = s.buffer;
      rx_tail = (rx_tail + 1) % kN;
      ++rx_used;
      fence();
      w32(kRdt, rx_tail);
      any = true;
    }
    // Кадры ядра → кольцо передачи.
    while (tx_used < kN - 1 && h->tx_head != __atomic_load_n(&h->tx_tail, __ATOMIC_ACQUIRE)) {
      abin::Slot s = *slot(abin::kTxOffset, h->tx_head);
      __atomic_store_n(&h->tx_head, h->tx_head + 1, __ATOMIC_RELEASE);
      if (s.buffer >= kBuffers || s.length > kBufferSize) continue;
      tx[tx_tail].addr = pool_iova + uint64_t(s.buffer) * kBufferSize;
      tx[tx_tail].length = static_cast<uint16_t>(s.length);
      tx[tx_tail].cso = 0;
      tx[tx_tail].cmd = kTxEop | kTxIfcs | kTxRs;
      tx[tx_tail].status = 0;
      tx[tx_tail].css = 0;
      tx[tx_tail].special = 0;
      tx_buf[tx_tail] = s.buffer;
      tx_tail = (tx_tail + 1) % kN;
      ++tx_used;
      fence();
      w32(kTdt, tx_tail);
      any = true;
    }
    // Готовое устройством (DD).
    fence();
    while (rx_used != 0 && (rx[rx_next].status & kRxDd) != 0) {
      uint32_t t = h->rx_done_tail;
      // Ошибки приёма (§3.2.3.2): RXE, SEQ, SE, CE. IPE/TCPE — итог аппаратной проверки
      // сумм; суммы проверяет стек ядра, по ним кадр не отбрасываем.
      bool good = (rx[rx_next].errors & 0x87) == 0 && rx[rx_next].length >= 14;
      *slot(abin::kRxDoneOffset, t) = {rx_buf[rx_next], rx[rx_next].length,
                                       good ? 0 : static_cast<int32_t>(Status::IoError), 0};
      __atomic_store_n(&h->rx_done_tail, t + 1, __ATOMIC_RELEASE);
      rx_next = (rx_next + 1) % kN;
      --rx_used;
      any = true;
    }
    while (tx_used != 0 && (tx[tx_clean].status & kTxDd) != 0) {
      uint32_t t = h->tx_done_tail;
      *slot(abin::kTxDoneOffset, t) = {tx_buf[tx_clean], 0, 0, 0};
      __atomic_store_n(&h->tx_done_tail, t + 1, __ATOMIC_RELEASE);
      tx_clean = (tx_clean + 1) % kN;
      --tx_used;
      any = true;
    }
    if (any) {
      if (__atomic_load_n(&h->kernel_wants_kick, __ATOMIC_ACQUIRE) != 0) {
        __atomic_store_n(&h->kernel_wants_kick, 0u, __ATOMIC_RELAXED);
        char none = 0;
        (void)channel_write(note, &none, 0, nullptr, 0);
      }
      continue;
    }
    // Повторная запись RDT в простое. Не требование SDM, а наблюдаемое
    // поведение модели QEMU: входящие, отложенные при заполненном кольце,
    // выдаются снова только по записи RDT при свободных дескрипторах. На железе
    // запись того же значения ничего не меняет (m7/tcp_e1000 без неё стоит).
    w32(kRdt, rx_tail);
    // Опрос: короткий сон на порту (звонок ядра или закрытие канала будят раньше).
    __atomic_store_n(&h->driver_wants_kick, 1u, __ATOMIC_SEQ_CST);
    if (!kick_armed) kick_armed = object_bind(port_h, note, 2) == Status::Ok;
    if (!kernel_armed) kernel_armed = object_bind(port_h, kernel, 3) == Status::Ok;
    PortPacket pk;
    if (port_wait(port_h, &pk, now_ns() + 1'000'000) != Status::Ok) continue;
    if (pk.key == 2) {
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
