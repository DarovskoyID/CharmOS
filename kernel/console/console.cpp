#include "console.hpp"

#include <arch/x86_64/serial.hpp>
#include <lib/acpi/tables.hpp>

#include "../core/log.hpp"
#include "../ipc/ipc.hpp"
#include "../platform/platform.hpp"
#include "../proc/uaccess.hpp"
#include "../sched/sched.hpp"
#include "../smp/cpu.hpp"
#include "../sync/spinlock.hpp"

namespace kernel::console {

namespace {

namespace rights = abi::syscall::rights;
using arch::x86_64::Serial;

constexpr uint32_t kRing = 4096;  // console.md §1
constexpr uint8_t kIsaIrqCom1 = 4;
constexpr uint8_t kCtrlC = 0x03, kCtrlBackslash = 0x1c;

constinit sync::Spinlock g_lock{sync::LockRank::Block, "console"};
constinit uint8_t g_ring[kRing] = {};
constinit uint32_t g_head = 0, g_count = 0;
constinit void (*g_control)(uint32_t) = nullptr;
constinit uint64_t g_lost = 0;

void irq_handler(arch::x86_64::InterruptFrame&) {
  uint32_t signal = 0;
  {
    sync::SpinGuard guard(g_lock);
    while (Serial::rx_ready()) {
      uint8_t b = Serial::get();
      if (b == kCtrlC) {
        signal = 1;
      } else if (b == kCtrlBackslash) {
        signal = 2;
      } else if (g_count == kRing) {
        ++g_lost;  // переполнение — байт теряется (console.md §1)
      } else {
        g_ring[(g_head + g_count) % kRing] = b;
        ++g_count;
      }
    }
  }
  platform::local_apic().eoi();
  ipc::console_readable();  // подписки портов (console.md §1)
  if (signal != 0 && g_control != nullptr) g_control(signal);
}

}  // namespace

void init() {
  // GSI для ISA IRQ 4: переопределение из MADT или тождественно.
  uint32_t gsi = kIsaIrqCom1;
  bool level = false, low = false;
  const auto& m = platform::madt();
  for (size_t i = 0; i < m.override_count; ++i) {
    if (m.overrides[i].source == kIsaIrqCom1) {
      gsi = m.overrides[i].gsi;
      low = (m.overrides[i].flags & lib::acpi::kIntiPolarityMask) == lib::acpi::kIntiPolarityLow;
      level = (m.overrides[i].flags & lib::acpi::kIntiTriggerMask) == lib::acpi::kIntiTriggerLevel;
    }
  }
  const auto* io = platform::io_apic_for(gsi);
  if (io == nullptr) {
    kprint("console: no I/O APIC for COM1 (GSI {}), input disabled\n", gsi);
    return;
  }
  uint8_t vector = platform::alloc_device_vector(&irq_handler);
  if (vector == 0) {
    kprint("console: no vector, input disabled\n");
    return;
  }
  io->route(gsi, {vector, level, low, static_cast<uint8_t>(smp::cpu(0).apic_id)});
  {
    sync::SpinGuard guard(g_lock);
    while (Serial::rx_ready()) (void)Serial::get();  // то, что пришло до нас (прошивка)
  }
  ipc::set_console_input_probe(&readable);
  Serial::enable_rx_interrupt();
  kprint("console: COM1 input on GSI {}\n", gsi);
}

Status console_cap(ipc::Cap** out) {
  return ipc::create_console(rights::kDuplicate | rights::kTransfer | rights::kRead | rights::kWrite, out);
}

void set_control_handler(void (*handler)(uint32_t)) { g_control = handler; }

bool readable() {
  sync::SpinGuard guard(g_lock);
  return g_count != 0;
}

Status read(void* buf, uint32_t cap, uint64_t deadline, uint32_t* got) {
  *got = 0;
  for (;;) {
    {
      sync::SpinGuard guard(g_lock);
      if (g_count != 0) {
        uint32_t n = cap < g_count ? cap : g_count;
        for (uint32_t i = 0; i < n; ++i) static_cast<uint8_t*>(buf)[i] = g_ring[(g_head + i) % kRing];
        g_head = (g_head + n) % kRing;
        g_count -= n;
        *got = n;
        return Status::Ok;
      }
    }
    if (deadline == 0) return Status::WouldBlock;
    if (platform::now_ns() >= deadline) return Status::TimedOut;
    sched::sleep_ns(2'000'000);  // опрос кольца (правило 9: очередь ожидания — после замеров)
  }
}

void write(const char* buf, uint64_t len) {
  sync::SpinGuard guard(log_lock());
  lib::core::Sink& s = log_sink();
  s.write(buf, len);
}

Status sys_console_read(uint32_t con, uint64_t ubuf, uint64_t cap, uint64_t deadline, uint64_t* out) {
  *out = 0;
  Status s = ipc::get_console(con, rights::kRead);
  if (s != Status::Ok) return s;
  if (cap == 0) return Status::InvalidArgs;
  uint8_t tmp[512];
  uint32_t n = cap < sizeof tmp ? static_cast<uint32_t>(cap) : sizeof tmp;
  uint32_t got;
  s = read(tmp, n, deadline, &got);
  if (s != Status::Ok) return s;
  if (!proc::copy_to_user(ubuf, tmp, got)) return Status::BadAddress;
  *out = got;
  return Status::Ok;
}

Status sys_console_write(uint32_t con, uint64_t ubuf, uint64_t len, uint64_t* out) {
  *out = 0;
  Status s = ipc::get_console(con, rights::kWrite);
  if (s != Status::Ok) return s;
  char tmp[512];
  for (uint64_t done = 0; done < len;) {
    uint64_t n = len - done < sizeof tmp ? len - done : sizeof tmp;
    if (!proc::copy_from_user(tmp, ubuf + done, n)) return done != 0 ? Status::Ok : Status::BadAddress;
    write(tmp, n);
    done += n;
    *out = done;
  }
  return Status::Ok;
}

}  // namespace kernel::console
