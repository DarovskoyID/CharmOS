// Драйвер Realtek RTL8139: регистры — даташит docs/specs/rtl8139d.pdf (§5:
// CR, IMR/ISR, TCR, RCR, TSD/TSAD, MSR); раскладка приёмного кольца
// (заголовок пакета, шаг, смещение CAPR) — docs/specs/rtl8139-rx-ring.md:
// источник вторичный (сводка автора по Programmer's Guide), в даташите
// этого нет. Прерывание — INTx (у чипа нет MSI), линия по _PRT (driver.md §10).
// С ядром — по протоколу docs/abi/net.md, как e1000.
#include <abi/net.hpp>

#include "../../tests/check.hpp"

using namespace charm;
namespace abin = abi::net;

namespace {

// Регистры (даташит §3, операционная карта).
constexpr uint64_t kIdr0 = 0x00, kMar0 = 0x08, kTsd0 = 0x10, kTsad0 = 0x20, kRbstart = 0x30;
constexpr uint64_t kCr = 0x37, kCapr = 0x38, kImr = 0x3c, kIsr = 0x3e, kTcr = 0x40, kRcr = 0x44;
constexpr uint64_t kMpc = 0x4c, kConfig1 = 0x52, kMsr = 0x58;
constexpr uint8_t kCrRst = 1u << 4, kCrRe = 1u << 3, kCrTe = 1u << 2, kCrBufe = 1u << 0;  // §5.4
constexpr uint8_t kMsrLinkb = 1u << 2;                                                  // §5.12: 0 — линк есть
// IMR/ISR (§5.5, §5.6): ROK, RER, TOK, TER, RXOVW, PUN/LinkChg, FOVW.
constexpr uint16_t kIntMask = 0x7f;
// TSD (§5.2): OWN — DMA дескриптора завершён; TOK; TUN; TABT; ERTXTH (21:16, ×32 байта).
constexpr uint32_t kTsdOwn = 1u << 13, kTsdErtxth256 = 8u << 16;
// TCR (§5.7): IFG = 11 (стандартный зазор), MXDMA = 110 (1024 байта).
constexpr uint32_t kTcrValue = 3u << 24 | 6u << 8;
// RCR (§5.8): RXFTH = 110 (1024 байта; не «без порога» — rtl8139-rx-ring.md §6),
// RBLEN = 00 (8 КиБ + 16), MXDMA = 111, WRAP = 1, AB, AM, APM.
constexpr uint32_t kRcrValue = 6u << 13 | 7u << 8 | 1u << 7 | 1u << 3 | 1u << 2 | 1u << 1;
// Заголовок пакета (rtl8139-rx-ring.md §1): младшие 16 бит — статус (RSR, §5.1), старшие — длина с CRC.
constexpr uint16_t kRsrRok = 1u << 0;
constexpr uint16_t kRxPending = 0xfff0;  // длина «ещё дописывается» (§5)

constexpr uint32_t kRingSize = 8192;
constexpr uint32_t kRingAlloc = 16384;  // 8192 + 16 + 1536 (WRAP = 1, §6), до страниц
constexpr uint32_t kMaxFrame = 1514;    // Ethernet без CRC
constexpr uint32_t kTxDescs = 4;
constexpr uint32_t kMinFrame = 60;  // без CRC (её дописывает чип)
constexpr uint32_t kBufferSize = 2048;
constexpr uint32_t kBuffers = 256;

uint64_t g_regs;
uint8_t r8(uint64_t o) { return *reinterpret_cast<volatile uint8_t*>(g_regs + o); }
uint16_t r16(uint64_t o) { return *reinterpret_cast<volatile uint16_t*>(g_regs + o); }
uint32_t r32(uint64_t o) { return *reinterpret_cast<volatile uint32_t*>(g_regs + o); }
void w8(uint64_t o, uint8_t v) { *reinterpret_cast<volatile uint8_t*>(g_regs + o) = v; }
void w16(uint64_t o, uint16_t v) { *reinterpret_cast<volatile uint16_t*>(g_regs + o) = v; }
void w32(uint64_t o, uint32_t v) { *reinterpret_cast<volatile uint32_t*>(g_regs + o) = v; }
void fence() { __atomic_thread_fence(__ATOMIC_SEQ_CST); }

uint64_t g_ring_iova;

// Сброс и настройка приёма/передачи; приёмное кольцо — с начала.
int64_t start_chip() {
  w8(kCr, kCrRst);
  uint64_t deadline = now_ns() + 1'000'000'000;
  while ((r8(kCr) & kCrRst) != 0) {
    EXPECT(now_ns() < deadline);
    yield();
  }
  // IOVA выдаются с 1 МиБ подряд (iommu), кольцо и пул — ниже 4 ГиБ: RBSTART и TSAD 32-битные.
  EXPECT((g_ring_iova >> 32) == 0);
  w32(kRbstart, static_cast<uint32_t>(g_ring_iova));
  for (uint64_t i = 0; i < 8; ++i) w8(kMar0 + i, 0);
  w8(kCr, kCrRe | kCrTe);  // RCR/TCR — после включения (как в Programmer's Guide)
  w32(kRcr, kRcrValue);
  w32(kTcr, kTcrValue);
  w16(kCapr, static_cast<uint16_t>(0u - 16u));  // CAPR смещён на 16 (rtl8139-rx-ring.md §4)
  w32(kMpc, 0);
  return 0;
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
  // BAR1 — те же регистры в памяти (BAR0 — порты ввода-вывода, их ABI не даёт).
  auto bar = syscall2(Number::DeviceMapBar, dev, 1);
  if (!bar.ok()) {
    print("rtl8139: cannot map BAR1\n");
    return 3;
  }
  auto map = vmo_map(static_cast<Handle>(bar.value), rights::kRead | rights::kWrite);
  EXPECT(map.ok());
  g_regs = map.value;

  w8(kConfig1, 0);  // выход из энергосбережения (LWAKE, PMEn)
  uint8_t mac[6];
  for (uint64_t i = 0; i < 6; ++i) mac[i] = r8(kIdr0 + i);  // загружен из EEPROM

  auto ring = dma_alloc(dev, kRingAlloc, abi::syscall::kDmaContiguous, &g_ring_iova);
  EXPECT(ring.ok());
  auto ring_map = vmo_map(static_cast<Handle>(ring.value), rights::kRead | rights::kWrite);
  EXPECT(ring_map.ok());
  auto* rx = reinterpret_cast<volatile uint8_t*>(ring_map.value);
  uint64_t pool_iova;
  auto pool = dma_alloc(dev, uint64_t{kBuffers} * kBufferSize, abi::syscall::kDmaContiguous, &pool_iova);
  EXPECT(pool.ok());
  EXPECT(((pool_iova + uint64_t{kBuffers} * kBufferSize) >> 32) == 0);
  // Пул отдаём ядру (хендл переходит), а отображение оставляем: в него копируем приём.
  auto pool_map = vmo_map(static_cast<Handle>(pool.value), rights::kRead | rights::kWrite);
  EXPECT(pool_map.ok());
  auto* pool_mem = reinterpret_cast<uint8_t*>(pool_map.value);
  auto kr = vmo_create(abin::kRingBytes);
  EXPECT(kr.ok());
  auto krmap = vmo_map(static_cast<Handle>(kr.value), rights::kRead | rights::kWrite);
  EXPECT(krmap.ok());

  EXPECT(start_chip() == 0);

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

  auto port = port_create();
  EXPECT(port.ok());
  Handle port_h = static_cast<Handle>(port.value);
  Handle irq_h = 0;
  if (info.irq_count >= 1) {
    auto irq = syscall2(Number::DeviceIrq, dev, 0);
    EXPECT(irq.ok());
    irq_h = static_cast<Handle>(irq.value);
    w16(kIsr, r16(kIsr));
    w16(kImr, kIntMask);
    print("rtl8139: INTx\n");
  }
  print("rtl8139: ready\n");

  uint32_t rx_off = 0;                          // смещение следующего заголовка в кольце
  uint32_t tx_buf[kTxDescs];
  uint32_t tx_next = 0, tx_clean = 0, tx_used = 0;  // дескрипторы по кругу (§5.2)
  bool kick_armed = false, kernel_armed = false, irq_armed = false;

  for (;;) {
    __atomic_store_n(&h->link_up, (r8(kMsr) & kMsrLinkb) == 0 ? 1u : 0u, __ATOMIC_RELEASE);
    bool any = false;
    // Приём: пока кольцо не пусто (BUFE) и у ядра есть пустые буферы.
    fence();
    while ((r8(kCr) & kCrBufe) == 0 && h->rx_free_head != __atomic_load_n(&h->rx_free_tail, __ATOMIC_ACQUIRE)) {
      uint32_t hdr = *reinterpret_cast<volatile uint32_t*>(rx + rx_off);
      uint16_t status = static_cast<uint16_t>(hdr);
      uint16_t len = static_cast<uint16_t>(hdr >> 16);
      if (len == kRxPending) break;
      if (len < 8 || len > kMaxFrame + 4 || (status & kRsrRok) == 0) {
        // Кольцо испорчено: без сброса приёма из него не выбраться (rtl8139-rx-ring.md §5).
        print("rtl8139: bad rx header, restarting receiver\n");
        EXPECT(start_chip() == 0);
        rx_off = 0;
        // Сброс начинает передачу с TSAD0 (§5.4): незавершённое — ядру как ошибку.
        for (; tx_used != 0; --tx_used, tx_clean = (tx_clean + 1) % kTxDescs) {
          uint32_t t = h->tx_done_tail;
          *slot(abin::kTxDoneOffset, t) = {tx_buf[tx_clean], 0, static_cast<int32_t>(Status::IoError), 0};
          __atomic_store_n(&h->tx_done_tail, t + 1, __ATOMIC_RELEASE);
        }
        tx_next = tx_clean = 0;
        break;
      }
      abin::Slot s = *slot(abin::kRxFreeOffset, h->rx_free_head);
      __atomic_store_n(&h->rx_free_head, h->rx_free_head + 1, __ATOMIC_RELEASE);
      if (s.buffer < kBuffers) {
        uint32_t frame = len - 4u;  // без CRC
        uint8_t* dst = pool_mem + uint64_t{s.buffer} * kBufferSize;
        for (uint32_t i = 0; i < frame; ++i) dst[i] = rx[rx_off + 4 + i];  // WRAP = 1: подряд за концом
        uint32_t t = h->rx_done_tail;
        *slot(abin::kRxDoneOffset, t) = {s.buffer, frame, 0, 0};
        __atomic_store_n(&h->rx_done_tail, t + 1, __ATOMIC_RELEASE);
      }
      rx_off = (rx_off + len + 4u + 3u) & ~3u;
      rx_off %= kRingSize;
      w16(kCapr, static_cast<uint16_t>(rx_off - 16u));
      any = true;
    }
    // Передача: кадры ядра → свободные дескрипторы.
    while (tx_used < kTxDescs && h->tx_head != __atomic_load_n(&h->tx_tail, __ATOMIC_ACQUIRE)) {
      abin::Slot s = *slot(abin::kTxOffset, h->tx_head);
      __atomic_store_n(&h->tx_head, h->tx_head + 1, __ATOMIC_RELEASE);
      if (s.buffer >= kBuffers || s.length > kMaxFrame) {
        uint32_t t = h->tx_done_tail;
        *slot(abin::kTxDoneOffset, t) = {s.buffer, 0, static_cast<int32_t>(Status::InvalidArgs), 0};
        __atomic_store_n(&h->tx_done_tail, t + 1, __ATOMIC_RELEASE);
        continue;
      }
      uint32_t size = s.length < kMinFrame ? kMinFrame : s.length;  // короткий — дополняем (байты буфера)
      tx_buf[tx_next] = s.buffer;
      w32(kTsad0 + 4u * tx_next, static_cast<uint32_t>(pool_iova + uint64_t{s.buffer} * kBufferSize));
      fence();
      w32(kTsd0 + 4u * tx_next, size | kTsdErtxth256);  // запись размера с OWN = 0 запускает передачу
      tx_next = (tx_next + 1) % kTxDescs;
      ++tx_used;
      any = true;
    }
    while (tx_used != 0 && (r32(kTsd0 + 4u * tx_clean) & kTsdOwn) != 0) {
      uint32_t t = h->tx_done_tail;
      *slot(abin::kTxDoneOffset, t) = {tx_buf[tx_clean], 0, 0, 0};
      __atomic_store_n(&h->tx_done_tail, t + 1, __ATOMIC_RELEASE);
      tx_clean = (tx_clean + 1) % kTxDescs;
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
    __atomic_store_n(&h->driver_wants_kick, 1u, __ATOMIC_SEQ_CST);
    if (h->rx_free_head != __atomic_load_n(&h->rx_free_tail, __ATOMIC_ACQUIRE) && (r8(kCr) & kCrBufe) == 0) continue;
    if (tx_used < kTxDescs && h->tx_head != __atomic_load_n(&h->tx_tail, __ATOMIC_ACQUIRE)) continue;
    if (irq_h != 0 && !irq_armed) irq_armed = object_bind(port_h, irq_h, 1) == Status::Ok;
    if (!kick_armed) kick_armed = object_bind(port_h, note, 2) == Status::Ok;
    if (!kernel_armed) kernel_armed = object_bind(port_h, kernel, 3) == Status::Ok;
    PortPacket pk;
    // Без линии — опрос; с линией — страховочный таймаут.
    uint64_t wait = irq_h != 0 ? 100'000'000 : 1'000'000;
    if (port_wait(port_h, &pk, now_ns() + wait) != Status::Ok) continue;
    if (pk.key == 1) {
      irq_armed = false;
      // §5.6: ISR сбрасывается чтением; запись прочитанного — на случай моделей,
      // где биты сбрасываются записью единиц (на чипе по даташиту ничего не делает).
      w16(kIsr, r16(kIsr));
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
