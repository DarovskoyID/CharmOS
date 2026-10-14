// Драйвер учебного устройства QEMU edu (docs/specs/qemu-edu.rst), M5.1–5.2:
// MMIO через BAR0 и прерывание MSI через порт. После проверок намеренно
// падает — тест перезапуска драйвера ядром (docs/abi/driver.md §8).
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
constexpr uint32_t kStatusIrqOnFactorial = 0x80;
constexpr uint32_t kIrqFactorial = 0x01;

}  // namespace

extern "C" int64_t charm_main(Handle kernel_channel) {
  char buf[16];
  Handle hs[1];
  uint32_t n, nh;
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

  // Намеренное падение: ядро должно перезапустить драйвер.
  *reinterpret_cast<volatile int*>(0) = 0;
  return 0;
}
