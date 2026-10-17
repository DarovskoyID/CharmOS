#include "platform.hpp"

#include <arch/x86_64/cpu.hpp>
#include <arch/x86_64/idt.hpp>
#include <lib/core/math.hpp>

#include "../core/cmdline.hpp"
#include "../core/panic.hpp"
#include "../sync/spinlock.hpp"
#include "../mm/layout.hpp"
#include "../mm/vmm.hpp"
#include "../proc/process.hpp"
#include "../proc/signal.hpp"
#include "../sched/sched.hpp"
#include "../smp/cpu.hpp"

namespace kernel::platform {

using arch::x86_64::Hpet;
using arch::x86_64::InterruptFrame;
using arch::x86_64::IoApic;
using arch::x86_64::LocalApic;
using lib::acpi::AcpiError;
using lib::acpi::MadtInfo;
using lib::acpi::SdtHeader;

namespace {

constinit MadtInfo g_madt = {};
constinit lib::acpi::FadtInfo g_fadt = {};
constinit LocalApic g_lapic;
constinit IoApic g_ioapics[MadtInfo::kMaxIoApics];
constinit size_t g_ioapic_count = 0;
constinit Hpet g_hpet;
constinit bool g_have_hpet = false;
constinit arch::x86_64::PmTimer g_pmt;
constinit RefClock g_ref;
constinit uint64_t g_tsc_hz = 0;
constinit bool g_realtime_valid=false;
constinit uint64_t g_realtime_base=0, g_realtime_monotonic=0;
constinit uint64_t g_apic_timer_hz = 0;
constinit volatile uint64_t g_ticks = 0;
constinit bool g_dump_acpi = false;
constinit const SdtHeader* g_xsdt = nullptr;

// Таблицы ACPI лежат либо в RAM (AcpiReclaimable/AcpiNvs — уже в HHDM), либо
// в зарезервированной памяти — тогда отображаем отдельно.
const void* map_acpi(void*, uint64_t phys, size_t size) {
  uint64_t p = 0, f = 0;
  uint64_t last = phys + (size == 0 ? 0 : size - 1);
  if (mm::translate(mm::kHhdmBase + phys, &p, &f) && mm::translate(mm::kHhdmBase + last, &p, &f)) {
    return mm::phys_to_virt<const void>(phys);
  }
  auto r = mm::map_mmio(phys, size);
  return r.is_ok() ? reinterpret_cast<const void*>(r.value()) : nullptr;
}

// acpi=dump: DSDT и все SSDT в serial шестнадцатеричным дампом
// (tools/acpi-extract.py собирает из него файлы для хост-тестов AML).
void dump_table(const SdtHeader* t, const char* what, uint32_t index) {
  const auto* b = reinterpret_cast<const uint8_t*>(t);
  kprint("acpidump: begin {} {} {}\n", what, index, t->length);
  static const char kHex[] = "0123456789abcdef";
  char line[2 * 64 + 1];
  for (uint32_t off = 0; off < t->length; off += 64) {
    uint32_t n = t->length - off < 64 ? t->length - off : 64;
    for (uint32_t i = 0; i < n; ++i) {
      line[2 * i] = kHex[b[off + i] >> 4];
      line[2 * i + 1] = kHex[b[off + i] & 15];
    }
    line[2 * n] = '\0';
    kprint("acpidump: {}\n", lib::core::StringView(line, 2 * n));
  }
  kprint("acpidump: end\n");
}

const SdtHeader* require_table(const SdtHeader* xsdt, const char* sig) {
  auto r = lib::acpi::find_table(xsdt, sig, 0, &map_acpi, nullptr);
  if (r.is_err()) panic("acpi: {} table: {}", sig, lib::acpi::error_name(r.error()));
  return r.value();
}

void init_acpi(const abi::boot::BootInfo& info) {
  const void* rsdp = map_acpi(nullptr, info.acpi_rsdp, sizeof(lib::acpi::Rsdp));
  auto xsdt_phys = lib::acpi::parse_rsdp(rsdp);
  if (xsdt_phys.is_err()) panic("acpi: RSDP: {}", lib::acpi::error_name(xsdt_phys.error()));

  const auto* xsdt_head =
      static_cast<const SdtHeader*>(map_acpi(nullptr, xsdt_phys.value(), sizeof(SdtHeader)));
  if (xsdt_head == nullptr) panic("acpi: cannot map XSDT");
  const auto* xsdt =
      static_cast<const SdtHeader*>(map_acpi(nullptr, xsdt_phys.value(), xsdt_head->length));
  if (auto v = lib::acpi::validate_table(xsdt, xsdt_head->length, "XSDT"); v.is_err()) {
    panic("acpi: XSDT: {}", lib::acpi::error_name(v.error()));
  }

  g_xsdt = xsdt;
  auto madt_ok = lib::acpi::parse_madt(require_table(xsdt, "APIC"), &g_madt);
  if (madt_ok.is_err()) panic("acpi: MADT: {}", lib::acpi::error_name(madt_ok.error()));
  auto fadt_ok = lib::acpi::parse_fadt(require_table(xsdt, "FACP"), &g_fadt);
  if (fadt_ok.is_err()) panic("acpi: FADT: {}", lib::acpi::error_name(fadt_ok.error()));

  size_t enabled = 0;
  for (size_t i = 0; i < g_madt.cpu_count; ++i) enabled += g_madt.cpus[i].enabled ? 1 : 0;
  kprint("acpi: MADT cpus={} (enabled {}) ioapics={} overrides={} pcat_compat={}\n",
         g_madt.cpu_count, enabled, g_madt.io_apic_count, g_madt.override_count,
         g_madt.pcat_compat);
  kprint("acpi: FADT rev={} sci={} boot_arch={x} flags={x}\n", g_fadt.revision,
         g_fadt.sci_interrupt, g_fadt.iapc_boot_arch, g_fadt.flags);
  if (g_dump_acpi && g_fadt.dsdt_address != 0) {
    const auto* head = static_cast<const SdtHeader*>(map_acpi(nullptr, g_fadt.dsdt_address, sizeof(SdtHeader)));
    if (head != nullptr) {
      const auto* dsdt = static_cast<const SdtHeader*>(map_acpi(nullptr, g_fadt.dsdt_address, head->length));
      if (dsdt != nullptr) dump_table(dsdt, "DSDT", 0);
    }
    for (uint32_t i = 0;; ++i) {
      auto ssdt = lib::acpi::find_table(xsdt, "SSDT", i, &map_acpi, nullptr);
      if (ssdt.is_err()) break;
      dump_table(ssdt.value(), "SSDT", i);
    }
  }

  lib::acpi::HpetInfo hpet_info;
  auto hpet_table = lib::acpi::find_table(xsdt, "HPET", 0, &map_acpi, nullptr);
  if (hpet_table.is_err()) {
    // Без HPET — ACPI PM-таймер из FADT.
    if (g_fadt.pm_timer_address == 0) panic("acpi: neither HPET nor PM timer");
    uint32_t bits = (g_fadt.flags & lib::acpi::kFadtTmrValExt) != 0 ? 32 : 24;
    uint64_t mmio = 0;
    if (g_fadt.pm_timer_memory) {
      auto m = mm::map_mmio(g_fadt.pm_timer_address & ~uint64_t{0xfff}, 4096);
      if (m.is_err()) panic("pmtimer: cannot map");
      mmio = m.value() + (g_fadt.pm_timer_address & 0xfff);
    }
    g_pmt.init(static_cast<uint16_t>(g_fadt.pm_timer_address), mmio, bits);
    kprint("acpi: no HPET, using PM timer at {} {x} ({}-bit)\n", g_fadt.pm_timer_memory ? "mem" : "port",
           g_fadt.pm_timer_address, bits);
    return;
  }
  g_have_hpet = true;
  if (auto h = lib::acpi::parse_hpet(hpet_table.value(), &hpet_info); h.is_err()) {
    panic("acpi: HPET: {}", lib::acpi::error_name(h.error()));
  }
  auto hpet_mmio = mm::map_mmio(hpet_info.base_address, 1024);  // блок занимает 1 КиБ (§3.2.4)
  if (hpet_mmio.is_err()) panic("hpet: cannot map registers");
  if (!g_hpet.init(hpet_mmio.value())) panic("hpet: implausible capabilities");
  kprint("hpet: base={x} period={} fs ({} Hz), {} timers, {}-bit counter\n",
         hpet_info.base_address, g_hpet.period_fs(), g_hpet.frequency_hz(), g_hpet.timer_count(),
         g_hpet.counter_is_64bit() ? 64 : 32);
}

void apic_error_handler(InterruptFrame&) {
  kprint("lapic: error interrupt\n");
  g_lapic.eoi();
}

void spurious_handler(InterruptFrame&) {
  // Ложное прерывание: EOI не нужен (SDM §11.9).
}

constinit uint32_t g_tick_hz = 0;

void timer_handler(InterruptFrame& frame) {
  if (smp::this_cpu().index == 0) {
    g_ticks = g_ticks + 1;
    proc::alarm_tick(now_ns());
  }
  // EOI до планировщика: schedule() может переключить поток, и обработчик
  // «вернётся» нескоро — без EOI таймер этого CPU замолчал бы.
  g_lapic.eoi();
  sched::on_timer_tick();
  // Процесс, который ядро попросило убить, умирает, не возвращаясь в ring 3.
  if ((frame.cs & 3) == 3) {
    proc::check_kill();
    proc::deliver_signals(frame);
  }
}

void init_interrupt_controllers(bool force_xapic) {
  if (g_madt.pcat_compat) arch::x86_64::mask_legacy_pic();

  using Mode = LocalApic::Mode;
  uint64_t apic_base = arch::x86_64::rdmsr(arch::x86_64::kMsrApicBase);
  bool firmware_x2apic = (apic_base & arch::x86_64::kApicBaseX2Apic) != 0;
  // Из x2APIC в xAPIC без сброса не вернуться (SDM табл. 11-5), поэтому
  // включённый прошивкой x2APIC оставляем, даже если попросили xAPIC.
  Mode mode = firmware_x2apic || (arch::x86_64::cpu_has_x2apic() && !force_xapic)
                  ? Mode::X2Apic
                  : Mode::XApic;
  uint64_t mmio = 0;
  if (mode == Mode::XApic) {
    uint64_t base = apic_base & 0x000ffffffffff000ull;
    if (base != g_madt.local_apic_address) {
      panic("lapic: MSR base {x} != MADT {x}", base, g_madt.local_apic_address);
    }
    auto m = mm::map_mmio(base, 4096);
    if (m.is_err()) panic("lapic: cannot map registers");
    mmio = m.value();
  }
  g_lapic.configure(mode, mmio);

  arch::x86_64::set_interrupt_handler(kVectorSpurious, &spurious_handler);
  arch::x86_64::set_interrupt_handler(kVectorApicError, &apic_error_handler);
  init_this_cpu();
  kprint("lapic: {} id={} version={x}\n", mode == Mode::X2Apic ? "x2APIC" : "xAPIC", g_lapic.id(),
         g_lapic.version() & 0xff);

  for (size_t i = 0; i < g_madt.io_apic_count; ++i) {
    const lib::acpi::IoApicInfo& io = g_madt.io_apics[i];
    auto io_mmio = mm::map_mmio(io.address, 4096);
    if (io_mmio.is_err()) panic("ioapic: cannot map registers");
    g_ioapics[i].init(io_mmio.value(), io.gsi_base);
    kprint("ioapic: id={} base={x} gsi {}..{} version={x}\n", io.id, io.address, io.gsi_base,
           io.gsi_base + g_ioapics[i].gsi_count() - 1, g_ioapics[i].version());
  }
  g_ioapic_count = g_madt.io_apic_count;
}

void calibrate() {
  constexpr uint64_t kWindowNs = 50'000'000;  // 50 мс

  uint64_t h0 = g_ref.counter();
  uint64_t t0 = arch::x86_64::rdtsc();
  g_ref.wait_ns(kWindowNs);
  uint64_t t1 = arch::x86_64::rdtsc();
  uint64_t h1 = g_ref.counter();
  uint64_t ns = g_ref.ticks_to_ns(g_ref.elapsed(h0, h1));
  g_tsc_hz = lib::core::mul_div(t1 - t0, 1'000'000'000, ns);

  // Таймер APIC: однократный отсчёт с максимума. За 10 мс он не истечёт,
  // а прерывания всё равно запрещены; после замера таймер останавливается.
  g_lapic.timer_one_shot(kVectorTimer, arch::x86_64::kApicTimerDivide16, 0xffffffffu);
  uint64_t a0 = g_ref.counter();
  g_ref.wait_ns(kWindowNs / 5);
  uint32_t remaining = g_lapic.timer_current();
  uint64_t a1 = g_ref.counter();
  g_lapic.timer_stop();
  uint64_t counted = 0xffffffffu - uint64_t{remaining};
  uint64_t apic_ns = g_ref.ticks_to_ns(g_ref.elapsed(a0, a1));
  g_apic_timer_hz =
      lib::core::mul_div(counted, 1'000'000'000, apic_ns);

  // Признак invariant TSC (CPUID 80000007H) описан в SDM Vol. 2A/3B, которых
  // ещё нет в docs/specs — поэтому пока не проверяется.
  kprint("tsc: {} MHz\n", g_tsc_hz / 1'000'000);
  kprint("lapic: timer {} kHz at divide 16\n", g_apic_timer_hz / 1000);
}

}  // namespace

void init_this_cpu() {
  g_lapic.init_this_cpu(kVectorSpurious, kVectorApicError);
  if (g_madt.nmi_lint <= 1) g_lapic.set_lint_nmi(g_madt.nmi_lint);
}

void platform_init(const abi::boot::BootInfo& info, const Cmdline& cmdline) {
  g_dump_acpi = cmdline.get("acpi") == lib::core::StringView("dump");
  init_acpi(info);
  init_interrupt_controllers(cmdline.get("lapic") == lib::core::StringView("xapic"));
  calibrate();
  if(info.version_minor>=1 && info.size>=sizeof(abi::boot::BootInfo) && (info.flags&abi::boot::kFlagRealtime)) {
    g_realtime_base=info.realtime_unix_ns;
    g_realtime_monotonic=lib::core::mul_div(info.realtime_sample_tsc,1000000000,g_tsc_hz);
    g_realtime_valid=true;
    kprint("clock: firmware UTC available\n");
  }
}

const MadtInfo& madt() { return g_madt; }
const lib::acpi::FadtInfo& fadt() { return g_fadt; }
LocalApic& local_apic() { return g_lapic; }
const Hpet& hpet() { return g_hpet; }
bool has_hpet() { return g_have_hpet; }

const void* map_acpi_table(uint64_t phys, size_t size) { return map_acpi(nullptr, phys, size); }

const SdtHeader* find_acpi_table(const char* sig, uint32_t index) {
  if (g_xsdt == nullptr) return nullptr;
  auto r = lib::acpi::find_table(g_xsdt, sig, index, &map_acpi, nullptr);
  return r.is_ok() ? r.value() : nullptr;
}
const RefClock& ref_clock() { return g_ref; }

uint64_t RefClock::counter() const { return g_have_hpet ? g_hpet.counter() : g_pmt.counter(); }
uint64_t RefClock::elapsed(uint64_t from, uint64_t to) const {
  return g_have_hpet ? g_hpet.elapsed(from, to) : g_pmt.elapsed(from, to);
}
uint64_t RefClock::ns_to_ticks(uint64_t ns) const { return g_have_hpet ? g_hpet.ns_to_ticks(ns) : g_pmt.ns_to_ticks(ns); }
uint64_t RefClock::ticks_to_ns(uint64_t t) const { return g_have_hpet ? g_hpet.ticks_to_ns(t) : g_pmt.ticks_to_ns(t); }
void RefClock::wait_ns(uint64_t ns) const {
  if (g_have_hpet) {
    g_hpet.wait_ns(ns);
  } else {
    g_pmt.wait_ns(ns);
  }
}

const IoApic* io_apic_for(uint32_t gsi) {
  for (size_t i = 0; i < g_ioapic_count; ++i) {
    if (g_ioapics[i].owns(gsi)) return &g_ioapics[i];
  }
  return nullptr;
}

uint64_t tsc_hz() { return g_tsc_hz; }
uint64_t apic_timer_hz() { return g_apic_timer_hz; }

void start_local_tick(uint32_t hz) {
  uint64_t count = g_apic_timer_hz / hz;
  if (count == 0 || count > 0xffffffffu) panic("timer: cannot program {} Hz", hz);
  arch::x86_64::set_interrupt_handler(kVectorTimer, &timer_handler);
  g_lapic.timer_periodic(kVectorTimer, arch::x86_64::kApicTimerDivide16,
                         static_cast<uint32_t>(count));
  if (smp::this_cpu().index == 0) {
    g_tick_hz = hz;
    kprint("timer: periodic tick {} Hz (count {})\n", hz, count);
  }
}

namespace {
constexpr uint8_t kFirstDeviceVector = 0x30;
constexpr uint8_t kLastDeviceVector = 0xef;
constinit bool g_vector_used[256] = {};
constinit sync::Spinlock g_vector_lock{sync::LockRank::Vectors, "platform.vectors"};
}  // namespace

uint8_t alloc_device_vector(arch::x86_64::InterruptHandler handler) {
  sync::SpinGuard guard(g_vector_lock);
  for (uint32_t v = kFirstDeviceVector; v <= kLastDeviceVector; ++v) {
    if (g_vector_used[v]) continue;
    g_vector_used[v] = true;
    arch::x86_64::set_interrupt_handler(static_cast<uint8_t>(v), handler);
    return static_cast<uint8_t>(v);
  }
  return 0;
}

void free_device_vector(uint8_t vector) {
  sync::SpinGuard guard(g_vector_lock);
  arch::x86_64::set_interrupt_handler(vector, nullptr);
  g_vector_used[vector] = false;
}

uint64_t ticks() { return g_ticks; }
uint32_t tick_hz() { return g_tick_hz; }

uint64_t now_ns() { return lib::core::mul_div(arch::x86_64::rdtsc(), 1'000'000'000, g_tsc_hz); }
abi::syscall::Status realtime_ns(uint64_t* out) {
  using abi::syscall::Status;
  if (!out) return Status::InvalidArgs;
  if (!g_realtime_valid) return Status::NotSupported;
  uint64_t now=now_ns(); if(now<g_realtime_monotonic) return Status::OutOfRange;
  uint64_t elapsed=now-g_realtime_monotonic;
  if(elapsed>UINT64_MAX-g_realtime_base) return Status::OutOfRange;
  *out=g_realtime_base+elapsed; return Status::Ok;
}

}  // namespace kernel::platform
