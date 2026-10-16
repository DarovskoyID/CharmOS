// Own xHCI driver, Intel xHCI 1.2 sections 4.2, 4.9, 5, 6, 7.1.
// Root-port enumeration, control transfers and HID boot keyboard/mouse.
// External hubs, reconnect handling and storage are separate milestones.
#include <lib/usb/xhci.hpp>
#include <lib/usb/hid.hpp>
#include "../../tests/check.hpp"

using namespace charm;
namespace x = lib::usb::xhci;
namespace {
uint64_t base, op, runtime, bells;
Handle device, irq, port;
x::Producer commands;
x::Consumer events;
uint64_t bar_size;
uint64_t dcbaa_cpu;
uint32_t context_size;
struct Hid {
  lib::usb::BootInterface info{};
  x::Producer ring{};
  uint64_t report_cpu = 0, report_iova = 0;
  uint32_t length = 0, dci = 0;
  uint64_t pending = 0;
  lib::usb::BootKeyboard keyboard{};
  lib::usb::BootMouse mouse{};
};
struct UsbDevice {
  bool used = false;
  uint32_t slot = 0, port_number = 0, speed = 0;
  uint64_t input_cpu = 0, input_iova = 0, output_cpu = 0, output_iova = 0;
  uint64_t data_cpu = 0, data_iova = 0;
  x::Producer control{};
  Hid hid[8]{};
  uint32_t hid_count = 0;
};
UsbDevice devices[17];
void transfer_event(const x::Trb& event);

void log_value(const char* label, uint64_t value) {
  char out[128]; auto s = lib::core::format_to(out, sizeof out, "xhci: {} {}\n", label, value);
  debug_write(s.data(), s.size());
}

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
      if (((event.control >> 10) & 63) == 32) transfer_event(event);
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

void emit(void*, const lib::usb::HidEvent& event) {
  abi::syscall::InputEvent e{0, event.type, event.code, event.value, event.x, event.y};
  (void)syscall2(Number::DeviceInput, device, reinterpret_cast<uint64_t>(&e));
  if (e.type == abi::syscall::kInputKey && e.value == 1) log_value("HID key", e.code);
}
void submit_hid(UsbDevice& d, Hid& h) {
  h.pending = h.ring.push({h.report_iova, h.length, x::type(1) | (1u << 5) | (1u << 2)});
  reg<uint32_t>(bells + uint64_t{d.slot} * 4) = h.dci; fence();
}
void transfer_event(const x::Trb& event) {
  uint32_t slot = event.control >> 24, dci = (event.control >> 16) & 31;
  if (slot == 0 || slot >= 17 || !devices[slot].used) return;
  UsbDevice& d = devices[slot];
  for (uint32_t i = 0; i < d.hid_count; ++i) {
    Hid& h = d.hid[i];
    if (h.dci != dci || h.pending != event.parameter) continue;
    uint32_t code = event.status >> 24, remaining = event.status & 0xffffff;
    if ((code == 1 || code == 13) && remaining <= h.length) {
      const auto* report = reinterpret_cast<const uint8_t*>(h.report_cpu);
      size_t size = h.length - remaining;
      if (h.info.protocol == 1) (void)h.keyboard.decode(report, size, emit, nullptr);
      else (void)h.mouse.decode(report, size, emit, nullptr);
      submit_hid(d, h);
    } else log_value("HID transfer failed", code);
    return;
  }
}
bool wait_transfer(UsbDevice& d, uint64_t pointer) {
  uint64_t deadline = now_ns() + 2'000'000'000;
  for (;;) {
    x::Trb event{};
    while (events.pop(&event)) {
      reg<uint64_t>(runtime + 0x38) = events.dequeue() | 8; fence();
      if (((event.control >> 10) & 63) == 32 && (event.control >> 24) == d.slot &&
          ((event.control >> 16) & 31) == 1) {
        uint32_t code = event.status >> 24;
        if (code != 1 && code != 13) { log_value("control transfer failed", code); return false; }
        if (event.parameter == pointer) return true;
      } else if (((event.control >> 10) & 63) == 32) transfer_event(event);
    }
    PortPacket packet{};
    Status s = port_wait(port, &packet, deadline);
    if (s == Status::TimedOut || now_ns() >= deadline) return false;
    if (s == Status::Ok && packet.key == 1) {
      reg<uint32_t>(op + 4) = 8; reg<uint32_t>(runtime + 0x20) = 3; fence();
      (void)syscall1(Number::InterruptAck, irq); (void)object_bind(port, irq, 1);
    }
  }
}
bool control(UsbDevice& d, uint8_t request_type, uint8_t request, uint16_t value, uint16_t index, uint16_t length) {
  if (length > 4096) return false;
  bool in = (request_type & 0x80) != 0;
  uint64_t setup = request_type | (uint64_t{request} << 8) | (uint64_t{value} << 16) |
                   (uint64_t{index} << 32) | (uint64_t{length} << 48);
  uint32_t trt = length == 0 ? 0 : (in ? 3u : 2u);
  (void)d.control.push({setup, 8, x::type(2) | (1u << 6) | (trt << 16)});
  if (length != 0) (void)d.control.push({d.data_iova, length, x::type(3) | (in ? 1u << 16 : 0u)});
  uint64_t last = d.control.push({0, 0, x::type(4) | (1u << 5) | (length == 0 || !in ? 1u << 16 : 0u)});
  reg<uint32_t>(bells + uint64_t{d.slot} * 4) = 1; fence();
  return wait_transfer(d, last);
}
uint32_t* context(uint64_t cpu, uint32_t index) {
  return reinterpret_cast<uint32_t*>(cpu + uint64_t{index} * context_size);
}
void clear_input(UsbDevice& d) {
  auto* mem = reinterpret_cast<uint64_t*>(d.input_cpu);
  for (uint32_t i = 0; i < 4096 / 8; ++i) mem[i] = 0;
}
void endpoint(uint32_t* ctx, uint32_t type, uint16_t packet, uint64_t ring, uint32_t interval, uint32_t payload) {
  ctx[0] = interval << 16;
  ctx[1] = (3u << 1) | (type << 3) | (uint32_t{packet} << 16);
  ctx[2] = static_cast<uint32_t>(ring) | 1; ctx[3] = static_cast<uint32_t>(ring >> 32);
  ctx[4] = payload | (type == 7 ? payload << 16 : 0u);
}
bool enumerate(uint32_t root_port) {
  uint64_t portsc = op + 0x400 + uint64_t{root_port - 1} * 16;
  uint32_t status = reg<uint32_t>(portsc);
  if ((status & 1) == 0) return true;
  if ((status & 2) == 0) {
    // PORTSC has RW1C bits: do not copy them into a port-reset write.
    reg<uint32_t>(portsc) = (status & ((1u << 9) | (3u << 14) | (7u << 25))) | (1u << 4);
    if (!wait_bits(portsc, (1u << 4) | 2, 2)) return false;
  }
  status = reg<uint32_t>(portsc);
  uint32_t speed = (status >> 10) & 15;
  if (speed < 1 || speed > 4) return false;
  x::Trb completion{};
  if (!command({0, 0, x::type(9)}, &completion)) return false;
  uint32_t slot = completion.control >> 24;
  if (slot == 0 || slot >= 17) return false;
  UsbDevice& d = devices[slot]; d.used = true; d.slot = slot; d.port_number = root_port; d.speed = speed;
  uint64_t ring_cpu, ring_iova;
  if (!dma(4096, &d.input_cpu, &d.input_iova) || !dma(4096, &d.output_cpu, &d.output_iova) ||
      !dma(4096, &d.data_cpu, &d.data_iova) || !dma(4096, &ring_cpu, &ring_iova)) return false;
  d.control = {reinterpret_cast<volatile x::Trb*>(ring_cpu), ring_iova, 0, 1};
  reinterpret_cast<uint64_t*>(dcbaa_cpu)[slot] = d.output_iova;
  context(d.input_cpu, 0)[1] = 3;
  uint32_t* sc = context(d.input_cpu, 1);
  sc[0] = (speed << 20) | (1u << 27); sc[1] = root_port << 16;
  uint16_t max_packet = speed == 4 ? 512 : (speed == 3 ? 64 : 8);
  endpoint(context(d.input_cpu, 2), 4, max_packet, ring_iova, 0, 8); fence();
  if (!command({d.input_iova, 0, x::type(11) | (slot << 24)}, &completion)) return false;
  if (!control(d, 0x80, 6, 0x100, 0, 8)) return false;
  auto* data = reinterpret_cast<const uint8_t*>(d.data_cpu);
  if (speed == 4 && data[7] != 9) return false;
  uint16_t actual_packet = speed == 4 ? 512 : data[7];
  if (actual_packet != max_packet) {
    if (speed != 1 || (actual_packet != 8 && actual_packet != 16 && actual_packet != 32 && actual_packet != 64)) return false;
    clear_input(d); context(d.input_cpu, 0)[1] = 2;
    auto* ep = context(d.input_cpu, 2); ep[1] = uint32_t{actual_packet} << 16;
    fence(); if (!command({d.input_iova, 0, x::type(13) | (slot << 24)}, &completion)) return false;
  }
  if (!control(d, 0x80, 6, 0x100, 0, 18) || data[0] != 18 || data[1] != 1) return false;
  if (!control(d, 0x80, 6, 0x200, 0, 9)) return false;
  uint16_t bytes = static_cast<uint16_t>(uint16_t{data[2]} | (uint16_t{data[3]} << 8));
  if (bytes < 9 || bytes > 4096 || !control(d, 0x80, 6, 0x200, 0, bytes)) return false;
  lib::usb::BootInterface interfaces[8]{}; uint8_t config = 0;
  if (!lib::usb::boot_interfaces(data, bytes, interfaces, 8, &d.hid_count, &config)) return false;
  if (d.hid_count == 0) { log_value("unsupported USB device at port", root_port); return true; }
  clear_input(d); sc = context(d.input_cpu, 1);
  for (uint32_t i = 0; i < context_size / 4; ++i) sc[i] = context(d.output_cpu, 0)[i];
  sc[3] = 0; uint32_t add = 1, highest = 1;
  for (uint32_t i = 0; i < d.hid_count; ++i) {
    Hid& h = d.hid[i]; h.info = interfaces[i]; h.dci = (h.info.endpoint & 15u) * 2 + 1;
    h.length = h.info.protocol == 1 ? 8u : 3u;
    if (h.info.max_packet < h.length || !dma(4096, &ring_cpu, &ring_iova) || !dma(4096, &h.report_cpu, &h.report_iova)) return false;
    h.ring = {reinterpret_cast<volatile x::Trb*>(ring_cpu), ring_iova, 0, 1};
    uint32_t interval;
    if (speed >= 3) { if (h.info.interval > 16) return false; interval = h.info.interval - 1u; }
    else { interval = 3; for (uint32_t n = h.info.interval; n > 1; n >>= 1) ++interval; }
    endpoint(context(d.input_cpu, h.dci + 1), 7, h.info.max_packet, ring_iova, interval, h.length);
    add |= 1u << h.dci; if (h.dci > highest) highest = h.dci;
  }
  sc[0] = (sc[0] & 0x7ffffff) | (highest << 27); context(d.input_cpu, 0)[1] = add; fence();
  if (!command({d.input_iova, 0, x::type(12) | (slot << 24)}, &completion) || !control(d, 0, 9, config, 0, 0)) return false;
  for (uint32_t i = 0; i < d.hid_count; ++i) {
    Hid& h = d.hid[i];
    if (!control(d, 0x21, 11, 0, h.info.number, 0)) return false;
    if (h.info.protocol == 1 && !control(d, 0x21, 10, 0, h.info.number, 0)) return false;
    print(h.info.protocol == 1 ? "xhci: HID keyboard ready\n" : "xhci: HID mouse ready\n");
    submit_hid(d, h);
  }
  return true;
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
  context_size = (reg<uint32_t>(base + 0x10) & 4) != 0 ? 64u : 32u;
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
  if (slots > 16) reg<uint32_t>(op + 0x38) = 16;
  dcbaa_cpu = dcpu;
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
  EXPECT(syscall2(Number::DeviceInput, device, 0).status == Status::BadAddress);
  for (uint32_t p = 1; p <= ports; ++p) {
    if (!enumerate(p)) { log_value("enumeration failed at port", p); return 2; }
  }
  for (;;) {
    (void)object_bind(port, kernel, 2);
    PortPacket packet{};
    if (port_wait(port, &packet, kForever) != Status::Ok) continue;
    if (packet.key == 2 && packet.type == abi::syscall::kPacketPeerClosed) return 0;
    if (packet.key == 1) {
      x::Trb event{}; while (events.pop(&event)) if (((event.control >> 10) & 63) == 32) transfer_event(event);
      reg<uint64_t>(runtime + 0x38) = events.dequeue() | 8;
      reg<uint32_t>(op + 4) = 8; reg<uint32_t>(runtime + 0x20) = 3; fence();
      (void)syscall1(Number::InterruptAck, irq); (void)object_bind(port, irq, 1);
    }
  }
}
