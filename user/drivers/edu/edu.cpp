// Драйвер учебного устройства QEMU edu (docs/specs/qemu-edu.rst), M5:
// MMIO через BAR0, прерывание MSI через порт, DMA через буфер dma_alloc.
// После проверок намеренно ломается — тест перезапуска драйвера ядром
// (docs/abi/driver.md §8): с IOMMU — DMA мимо своего домена (ядро убивает
// драйвер по ошибке трансляции), без IOMMU — обращение по нулевому адресу.
#include "../../tests/check.hpp"

using namespace charm;

namespace {

// Регистры edu (qemu-edu.rst, «MMIO area spec»).
constexpr uint64_t kId = 0x00;
constexpr uint64_t kLiveness = 0x04;
constexpr uint64_t kFactorial = 0x08;
constexpr uint64_t kStatus = 0x20;
constexpr uint64_t kIrqStatus = 0x24;
constexpr uint64_t kIrqAck = 0x64;
constexpr uint64_t kDmaSrc = 0x80;
constexpr uint64_t kDmaDst = 0x88;
constexpr uint64_t kDmaCount = 0x90;
constexpr uint64_t kDmaCmd = 0x98;
constexpr uint32_t kStatusIrqOnFactorial = 0x80;
constexpr uint32_t kIrqFactorial = 0x01;
constexpr uint64_t kDmaStart = 0x01;
constexpr uint64_t kDmaToRam = 0x02;      // направление: из edu в память
constexpr uint64_t kEduBuffer = 0x40000;  // внутренний буфер edu, 4096 байт

}  // namespace

extern "C" int64_t charm_main(Handle kernel_channel) {
  char buf[16];
  Handle hs[1];
  uint32_t n, nh;
  auto ver = abi_version();
  EXPECT(ver.ok() && (ver.value >> 16) == 1 && (ver.value & 0xffff) >= 2);  // driver.md — v1.2+
  EXPECT(read_msg(kernel_channel, buf, sizeof buf, hs, 1, &n, &nh) == Status::Ok);
  EXPECT(same(buf, n, "device") && nh == 1);
  Handle dev = hs[0];

  abi::syscall::DeviceInfo info;
  EXPECT(syscall2(Number::DeviceInfo, dev, reinterpret_cast<uint64_t>(&info)).ok());
  EXPECT(info.vendor == 0x1234 && info.device_id == 0x11e8);
  EXPECT(info.bar_kind[0] == abi::syscall::kBarMem32 && info.bar_size[0] == (1u << 20));
  EXPECT(info.irq_count == 1);
  // Хендл устройства нельзя отдать или размножить.
  EXPECT(handle_duplicate(dev, rights::kRead).status == Status::AccessDenied);

  auto bar = syscall2(Number::DeviceMapBar, dev, 0);
  EXPECT(bar.ok());
  auto map = vmo_map(static_cast<Handle>(bar.value), rights::kRead | rights::kWrite);
  EXPECT(map.ok());
  volatile uint32_t* regs = reinterpret_cast<volatile uint32_t*>(map.value);
  auto reg = [&](uint64_t off) -> volatile uint32_t& { return regs[off / 4]; };

  EXPECT((reg(kId) & 0xff) == 0xed);
  reg(kLiveness) = 0x12345678u;
  EXPECT(reg(kLiveness) == ~0x12345678u);

  auto irq = syscall2(Number::DeviceIrq, dev, 0);
  EXPECT(irq.ok());
  Handle irq_h = static_cast<Handle>(irq.value);
  EXPECT(syscall2(Number::DeviceIrq, dev, 0).status == Status::Busy);
  auto port = port_create();
  EXPECT(port.ok());
  Handle port_h = static_cast<Handle>(port.value);
  EXPECT(object_bind(port_h, irq_h, 77) == Status::Ok);

  // Факториал с прерыванием по завершении.
  reg(kStatus) = kStatusIrqOnFactorial;
  reg(kFactorial) = 10;
  PortPacket pk;
  EXPECT(port_wait(port_h, &pk, now_ns() + 2'000'000'000) == Status::Ok);
  EXPECT(pk.key == 77 && pk.type == abi::syscall::kPacketInterrupt && pk.data0 >= 1);
  uint32_t why = reg(kIrqStatus);
  EXPECT((why & kIrqFactorial) != 0);
  reg(kIrqAck) = why;  // устройство снимает запрос
  EXPECT(syscall1(Number::InterruptAck, irq_h).ok());
  EXPECT(reg(kFactorial) == 3628800);
  print("edu: MMIO ok, factorial via MSI ok\n");

  uint64_t dev_addr = 0;
  if ((info.flags & abi::syscall::kDeviceDma) == 0) {
    EXPECT(dma_alloc(dev, 4096, 0, &dev_addr).status == Status::NotSupported);
    print("edu: no DMA without IOMMU\n");
    // Намеренное падение: ядро должно перезапустить драйвер.
    *reinterpret_cast<volatile int*>(0) = 0;
    return 0;
  }

  // Регистры DMA 64-битные (qemu-edu.rst: смещения от 0x80 допускают 8 байт).
  auto reg64 = [&](uint64_t off) -> volatile uint64_t& {
    return reinterpret_cast<volatile uint64_t*>(map.value)[off / 8];
  };
  auto dma = [&](uint64_t src, uint64_t dst, uint64_t count, uint64_t cmd) {
    reg64(kDmaSrc) = src;
    reg64(kDmaDst) = dst;
    reg64(kDmaCount) = count;
    reg64(kDmaCmd) = cmd | kDmaStart;
  };
  auto dma_wait = [&]() {
    uint64_t deadline = now_ns() + 2'000'000'000;
    while ((reg64(kDmaCmd) & kDmaStart) != 0) {
      if (now_ns() > deadline) return false;
      yield();
    }
    return true;
  };

  auto dbuf = dma_alloc(dev, 4096, 0, &dev_addr);
  EXPECT(dbuf.ok());
  EXPECT(dev_addr != 0 && dev_addr + 4096 <= (1u << 28));  // edu: 28-битный DMA
  auto bmap = vmo_map(static_cast<Handle>(dbuf.value), rights::kRead | rights::kWrite);
  EXPECT(bmap.ok());
  auto* mem = reinterpret_cast<volatile uint8_t*>(bmap.value);
  for (uint32_t i = 0; i < 256; ++i) mem[i] = static_cast<uint8_t>(i * 7 + 1);
  dma(dev_addr, kEduBuffer, 256, 0);  // память → edu
  EXPECT(dma_wait());
  dma(kEduBuffer, dev_addr + 2048, 256, kDmaToRam);  // edu → память
  EXPECT(dma_wait());
  for (uint32_t i = 0; i < 256; ++i) EXPECT(mem[2048 + i] == static_cast<uint8_t>(i * 7 + 1));
  print("edu: DMA both ways via IOMMU ok\n");

  // Намеренная ошибка: запись туда, где у домена ничего нет. Ядро гасит
  // устройство и убивает драйвер, не дожидаясь его.
  dma(kEduBuffer, 0x8000000, 256, kDmaToRam);
  for (;;) asm volatile("" ::: "memory");  // без системных вызовов: убьёт тик таймера
}
