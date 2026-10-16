// Own xHCI driver, Intel xHCI 1.2 sections 4.2, 4.9, 5, 6, 7.1.
// First milestone: ownership, reset, DMA rings, interrupt and No Op command.
// USB enumeration, HID and storage follow separately; no device claims yet.
#include <lib/usb/xhci.hpp>
#include "../../tests/check.hpp"

using namespace charm;
namespace x = lib::usb::xhci;
namespace {
uint64_t base, op, runtime, bells;
Handle device, irq, port;
x::Producer commands;
x::Consumer events;
uint64_t bar_size;

template <typename T> volatile T& reg(uint64_t addr) { return *reinterpret_cast<volatile T*>(addr); }
void fence() { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
bool wait_bits(uint64_t addr, uint32_t mask, uint32_t value, uint64_t timeout = 1'000'000'000) {
  uint64_t deadline = now_ns() + timeout;
  while ((reg<uint32_t>(addr) & mask) != value) { if (now_ns() >= deadline) return false; yield(); }
  return true;
}
bool dma(uint64_t bytes, uint64_t* cpu, uint64_t* iova) {
  auto v = dma_alloc(device, bytes, abi::syscall::kDmaContiguous, iova);
  if (!v.ok()) return false;
  auto m = vmo_map(static_cast<Handle>(v.value), rights::kRead | rights::kWrite);
  (void)handle_close(static_cast<Handle>(v.value));
  if (!m.ok()) return false;
  *cpu = m.value;
  if ((reg<uint32_t>(base + 0x10) & 1) == 0 && *iova + bytes > 0x100000000ull) return false;
  return true;
}
bool ownership() {
  uint32_t offset = (reg<uint32_t>(base + 0x10) >> 16) * 4;
  for (unsigned count = 0; offset != 0 && count < 256; ++count) {
    if (offset > bar_size || bar_size - offset < 8) return false;
    uint32_t cap = reg<uint32_t>(base + offset);
    if ((cap & 255) == 1) {
      reg<uint8_t>(base + offset + 3) = static_cast<uint8_t>(reg<uint8_t>(base + offset + 3) | 1u);
      fence();
      if (!wait_bits(base + offset, 1u << 16, 0)) return false;
      uint32_t ctl = reg<uint32_t>(base + offset + 4);
      reg<uint32_t>(base + offset + 4) = (ctl & ~0xe011u) | 0xe0000000u;
    }
    uint32_t next = ((cap >> 8) & 255) * 4;
    if (next == 0) return true;
    offset += next;
  }
  return offset == 0;
}
bool command(x::Trb trb, x::Trb* completion) {
  uint64_t pointer = commands.push(trb);
  reg<uint32_t>(bells) = 0; fence();
  uint64_t deadline = now_ns() + 2'000'000'000;
  for (;;) {
    x::Trb event{};
    while (events.pop(&event)) {
      reg<uint64_t>(runtime + 0x38) = events.dequeue() | 8; fence(); // ERDP + EHB
      if (((event.control >> 10) & 63) == x::kCommandCompletion && event.parameter == pointer) {
        *completion = event;
        return (event.status >> 24) == 1;
      }
    }
    PortPacket packet{};
    Status s = port_wait(port, &packet, deadline);
    if (s == Status::TimedOut || now_ns() >= deadline) return false;
    if (s == Status::Ok && packet.key == 1) {
      reg<uint32_t>(op + 4) = 8;             // USBSTS.EINT W1C
      reg<uint32_t>(runtime + 0x20) = 3;     // IMAN.IP W1C, IE
      fence(); (void)syscall1(Number::InterruptAck, irq);
      (void)object_bind(port, irq, 1);
    }
  }
}
}

extern "C" int64_t charm_main(Handle kernel) {
  char message[16]; Handle handles[1]; uint32_t n, nh;
  EXPECT(read_msg(kernel, message, sizeof message, handles, 1, &n, &nh) == Status::Ok);
  EXPECT(same(message, n, "device") && nh == 1); device = handles[0];
  abi::syscall::DeviceInfo info{};
  EXPECT(syscall2(Number::DeviceInfo, device, reinterpret_cast<uint64_t>(&info)).ok());
  EXPECT(info.class_code == 0x0c0330 && (info.flags & abi::syscall::kDeviceDma) != 0 && info.irq_count > 0);
  EXPECT(info.bar_kind[0] == abi::syscall::kBarMem32 || info.bar_kind[0] == abi::syscall::kBarMem64);
  bar_size = info.bar_size[0]; EXPECT(bar_size >= 0x1000);
  auto bar = syscall2(Number::DeviceMapBar, device, 0); EXPECT(bar.ok());
  auto map = vmo_map(static_cast<Handle>(bar.value), rights::kRead | rights::kWrite); EXPECT(map.ok());
  base = map.value; (void)handle_close(static_cast<Handle>(bar.value));
  uint32_t cap0 = reg<uint32_t>(base), caplen = cap0 & 255;
  uint32_t version = cap0 >> 16, hcs1 = reg<uint32_t>(base + 4), hcs2 = reg<uint32_t>(base + 8);
  uint32_t slots = hcs1 & 255, ports = hcs1 >> 24;
  uint32_t dboff = reg<uint32_t>(base + 0x14) & ~3u, rtsoff = reg<uint32_t>(base + 0x18) & ~31u;
  EXPECT(caplen >= 0x20 && slots > 0 && ports > 0 && caplen + 0x400 + ports * 16 <= bar_size);
  EXPECT(dboff < bar_size && bar_size - dboff >= (slots + 1) * 4);
  EXPECT(rtsoff < bar_size && bar_size - rtsoff >= 0x40);
  char diagnostic[128];
  auto formatted = lib::core::format_to(diagnostic, sizeof diagnostic, "xhci: version {x}, slots {}, ports {}\n", version, slots, ports);
  debug_write(formatted.data(), formatted.size());
  EXPECT(version >= 0x100 && version <= 0x120);
  op = base + caplen; runtime = base + rtsoff; bells = base + dboff;
  EXPECT(ownership()); EXPECT(wait_bits(op + 4, 1u << 11, 0));
  reg<uint32_t>(op) = reg<uint32_t>(op) & ~1u; EXPECT(wait_bits(op + 4, 1, 1));
  reg<uint32_t>(op) = 2; EXPECT(wait_bits(op, 2, 0)); EXPECT(wait_bits(op + 4, 1u << 11, 0));
  EXPECT((reg<uint32_t>(op + 8) & 1) != 0);

  uint64_t dcpu, diova, ccpu, ciova, ecpu, eiova, tcpu, tiova;
  EXPECT(dma(4096, &dcpu, &diova) && dma(4096, &ccpu, &ciova) && dma(4096, &ecpu, &eiova) && dma(4096, &tcpu, &tiova));
  uint32_t scratch = ((hcs2 >> 27) & 31) | (((hcs2 >> 21) & 31) << 5);
  if (scratch != 0) {
    uint64_t array, array_iova;
    EXPECT(dma((uint64_t{scratch} * 8 + 4095) & ~uint64_t{4095}, &array, &array_iova));
    for (uint32_t i = 0; i < scratch; ++i) {
      uint64_t cpu, iova; EXPECT(dma(4096, &cpu, &iova));
      reinterpret_cast<uint64_t*>(array)[i] = iova;
    }
    reinterpret_cast<uint64_t*>(dcpu)[0] = array_iova;
  }
  commands = {reinterpret_cast<volatile x::Trb*>(ccpu), ciova, 0, 1};
  events = {reinterpret_cast<volatile x::Trb*>(ecpu), eiova, 0, 1};
  *reinterpret_cast<x::ErstEntry*>(tcpu) = {eiova, 256, 0}; fence();
  reg<uint32_t>(op + 0x38) = slots;
  reg<uint64_t>(op + 0x30) = diova;
  reg<uint64_t>(op + 0x18) = ciova | 1;
  reg<uint32_t>(runtime + 0x28) = 1;
  reg<uint64_t>(runtime + 0x38) = eiova;
  reg<uint64_t>(runtime + 0x30) = tiova;
  auto ih = syscall2(Number::DeviceIrq, device, 0); EXPECT(ih.ok()); irq = static_cast<Handle>(ih.value);
  auto ph = port_create(); EXPECT(ph.ok()); port = static_cast<Handle>(ph.value);
  EXPECT(object_bind(port, irq, 1) == Status::Ok);
  reg<uint32_t>(runtime + 0x24) = 0; reg<uint32_t>(runtime + 0x20) = 3;
  fence(); reg<uint32_t>(op) = 5; EXPECT(wait_bits(op + 4, 1, 0));
  x::Trb completion{}; EXPECT(command({0, 0, x::type(x::kNoop)}, &completion));
  print("xhci: controller running, command ring ok\n");
  for (;;) {
    (void)object_bind(port, kernel, 2);
    PortPacket packet{};
    if (port_wait(port, &packet, kForever) != Status::Ok) continue;
    if (packet.key == 2 && packet.type == abi::syscall::kPacketPeerClosed) return 0;
    if (packet.key == 1) {
      x::Trb event{}; while (events.pop(&event)) {}
      reg<uint64_t>(runtime + 0x38) = events.dequeue() | 8;
      reg<uint32_t>(op + 4) = 8; reg<uint32_t>(runtime + 0x20) = 3; fence();
      (void)syscall1(Number::InterruptAck, irq); (void)object_bind(port, irq, 1);
    }
  }
}
