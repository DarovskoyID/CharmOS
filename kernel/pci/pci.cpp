#include "pci.hpp"

#include <lib/acpi/parse.hpp>

#include "../core/panic.hpp"
#include "../mm/layout.hpp"
#include "../mm/vmm.hpp"
#include "../platform/platform.hpp"

namespace kernel::pci {

namespace {

// Источник вторичный: docs/specs/pci-reference.md §1–3.
constexpr uint16_t kRegClassRevision = 0x08;  // revision | prog-if | subclass | class
constexpr uint16_t kRegHeaderType = 0x0e;
constexpr uint16_t kRegBar0 = 0x10;
constexpr uint16_t kRegCapPtr = 0x34;
constexpr uint16_t kRegSecondaryBus = 0x19;  // заголовок типа 1
constexpr uint16_t kRegStatus = 0x06;
constexpr uint16_t kStatusCapList = 1u << 4;
constexpr uint8_t kCapMsi = 0x05;
constexpr uint8_t kCapMsix = 0x11;
constexpr int kMaxCapWalk = 48;

struct Window {
  uint16_t segment;
  uint8_t start_bus;
  uint8_t end_bus;
  uint64_t virt;
  uint8_t visited[32];  // битовая карта посещённых шин (защита от циклов)
};

constinit Window g_windows[lib::acpi::McfgInfo::kMaxWindows] = {};
constinit uint32_t g_window_count = 0;
constinit Function g_functions[kMaxFunctions] = {};
constinit uint32_t g_count = 0;

uint64_t config_address(const Window& w, uint8_t bus, uint8_t dev, uint8_t fn) {
  return w.virt + (uint64_t{static_cast<uint8_t>(bus - w.start_bus)} << 20) +
         (uint64_t{dev} << 15) + (uint64_t{fn} << 12);
}

uint16_t raw_read16(uint64_t cfg, uint16_t off) {
  return *reinterpret_cast<volatile const uint16_t*>(cfg + off);
}
uint8_t raw_read8(uint64_t cfg, uint16_t off) {
  return *reinterpret_cast<volatile const uint8_t*>(cfg + off);
}

void decode_bars(Function& f) {
  if (f.header_type != 0) return;  // BAR разбираем только у конечных устройств
  // Определение размера: декодирование выключено, пока в BAR записаны единицы
  // (pci-reference.md §3), иначе устройство отвечало бы на чужие адреса.
  uint16_t command = read16(f, kRegCommand);
  write16(f, kRegCommand, command & static_cast<uint16_t>(~(kCommandIo | kCommandMemory)));
  for (int i = 0; i < 6; ++i) {
    auto off = static_cast<uint16_t>(kRegBar0 + 4 * i);
    uint32_t raw = read32(f, off);
    Bar& b = f.bars[i];
    if ((raw & 1) != 0) {
      write32(f, off, 0xffffffffu);
      uint32_t mask = read32(f, off) & ~0x3u;
      write32(f, off, raw);
      if (mask == 0) continue;
      b.kind = BarKind::Io;
      b.base = raw & ~0x3u;
      b.size = (~mask & 0xffffu) + 1;  // порты — 16 бит
      continue;
    }
    bool is64 = ((raw >> 1) & 0x3) == 2 && i < 5;
    b.prefetchable = ((raw >> 3) & 1) != 0;
    write32(f, off, 0xffffffffu);
    uint32_t mask_lo = read32(f, off) & ~0xfu;
    write32(f, off, raw);
    uint64_t base = raw & ~0xfu;
    uint64_t mask;
    if (is64) {
      auto hi = static_cast<uint16_t>(off + 4);
      uint32_t raw_hi = read32(f, hi);
      write32(f, hi, 0xffffffffu);
      uint32_t mask_hi = read32(f, hi);
      write32(f, hi, raw_hi);
      base |= uint64_t{raw_hi} << 32;
      mask = (uint64_t{mask_hi} << 32) | mask_lo;
    } else {
      // Маска 32-битного BAR: старшие 32 бита считаем единицами, чтобы
      // ~mask + 1 дало размер в пределах 32 бит.
      mask = mask_lo == 0 ? 0 : (0xffffffff00000000ull | mask_lo);
    }
    if (is64) ++i;  // следующий BAR — старшая половина этого (остаётся Absent)
    if (mask == 0) continue;  // не реализован
    b.kind = is64 ? BarKind::Mem64 : BarKind::Mem32;
    b.base = base;
    b.size = ~mask + 1;
  }
  write16(f, kRegCommand, command);
}

void find_caps(Function& f) {
  if ((read16(f, kRegStatus) & kStatusCapList) == 0) return;
  uint8_t ptr = read8(f, kRegCapPtr) & 0xfc;
  for (int n = 0; ptr != 0 && n < kMaxCapWalk; ++n) {
    uint8_t id = read8(f, ptr);
    if (id == kCapMsi && f.msi_cap == 0) f.msi_cap = ptr;
    if (id == kCapMsix && f.msix_cap == 0) f.msix_cap = ptr;
    ptr = read8(f, static_cast<uint16_t>(ptr + 1)) & 0xfc;
  }
}

void scan_bus(Window& w, uint8_t bus);

void scan_function(Window& w, uint8_t bus, uint8_t dev, uint8_t fn) {
  if (g_count == kMaxFunctions) return;
  Function& f = g_functions[g_count++];
  f = Function{};
  f.segment = w.segment;
  f.bus = bus;
  f.device = dev;
  f.function = fn;
  f.config = config_address(w, bus, dev, fn);
  f.vendor = read16(f, kRegVendor);
  f.device_id = read16(f, 0x02);
  uint32_t cr = read32(f, kRegClassRevision);
  f.revision = static_cast<uint8_t>(cr);
  f.class_code = cr >> 8;
  f.header_type = read8(f, kRegHeaderType) & 0x7f;
  decode_bars(f);
  find_caps(f);
  // PCI-to-PCI мост (класс 0x06, подкласс 0x04, заголовок типа 1).
  if ((f.class_code >> 8) == 0x0604 && f.header_type == 1) {
    uint8_t secondary = read8(f, kRegSecondaryBus);
    if (secondary >= w.start_bus && secondary <= w.end_bus) scan_bus(w, secondary);
  }
}

void scan_bus(Window& w, uint8_t bus) {
  uint8_t bit = static_cast<uint8_t>(1u << (bus & 7));
  if ((w.visited[bus >> 3] & bit) != 0) return;  // битый мост указывает на себя
  w.visited[bus >> 3] |= bit;
  for (uint8_t dev = 0; dev < 32; ++dev) {
    uint64_t cfg0 = config_address(w, bus, dev, 0);
    if (raw_read16(cfg0, kRegVendor) == 0xffff) continue;
    scan_function(w, bus, dev, 0);
    if ((raw_read8(cfg0, kRegHeaderType) & 0x80) == 0) continue;
    for (uint8_t fn = 1; fn < 8; ++fn) {
      if (raw_read16(config_address(w, bus, dev, fn), kRegVendor) != 0xffff) {
        scan_function(w, bus, dev, fn);
      }
    }
  }
}

const void* map_acpi_table(void*, uint64_t phys, size_t size) {
  uint64_t p = 0, fl = 0;
  if (mm::translate(mm::kHhdmBase + phys, &p, &fl) &&
      mm::translate(mm::kHhdmBase + phys + size - 1, &p, &fl)) {
    return reinterpret_cast<const void*>(mm::kHhdmBase + phys);
  }
  auto r = mm::map_mmio(phys, size);
  return r.is_ok() ? reinterpret_cast<const void*>(r.value()) : nullptr;
}

}  // namespace

uint8_t read8(const Function& f, uint16_t off) { return raw_read8(f.config, off); }
uint16_t read16(const Function& f, uint16_t off) { return raw_read16(f.config, off); }
uint32_t read32(const Function& f, uint16_t off) {
  return *reinterpret_cast<volatile const uint32_t*>(f.config + off);
}
void write16(const Function& f, uint16_t off, uint16_t v) {
  *reinterpret_cast<volatile uint16_t*>(f.config + off) = v;
}
void write32(const Function& f, uint16_t off, uint32_t v) {
  *reinterpret_cast<volatile uint32_t*>(f.config + off) = v;
}

void pci_init(const abi::boot::BootInfo& info) {
  const void* rsdp = map_acpi_table(nullptr, info.acpi_rsdp, sizeof(lib::acpi::Rsdp));
  auto xsdt_phys = lib::acpi::parse_rsdp(rsdp);
  if (xsdt_phys.is_err()) panic("pci: RSDP");
  const auto* head = static_cast<const lib::acpi::SdtHeader*>(
      map_acpi_table(nullptr, xsdt_phys.value(), sizeof(lib::acpi::SdtHeader)));
  const auto* xsdt = static_cast<const lib::acpi::SdtHeader*>(
      map_acpi_table(nullptr, xsdt_phys.value(), head->length));
  auto mcfg = lib::acpi::find_table(xsdt, "MCFG", 0, &map_acpi_table, nullptr);
  if (mcfg.is_err()) {
    kprint("pci: no MCFG ({}), no PCI devices\n", lib::acpi::error_name(mcfg.error()));
    return;
  }
  lib::acpi::McfgInfo m;
  if (auto r = lib::acpi::parse_mcfg(mcfg.value(), &m); r.is_err()) {
    kprint("pci: bad MCFG: {}\n", lib::acpi::error_name(r.error()));
    return;
  }
  for (size_t i = 0; i < m.count; ++i) {
    const lib::acpi::EcamWindow& e = m.windows[i];
    uint64_t size = (uint64_t{e.end_bus} - e.start_bus + 1) << 20;
    auto virt = mm::map_mmio(e.base + (uint64_t{e.start_bus} << 20), size);
    if (virt.is_err()) panic("pci: cannot map ECAM window");
    Window& w = g_windows[g_window_count++];
    w = Window{e.segment, e.start_bus, e.end_bus, virt.value(), {}};
    kprint("pci: ECAM segment {} buses {}..{} at {x}\n", e.segment, e.start_bus, e.end_bus, e.base);
    scan_bus(w, e.start_bus);
  }
  for (uint32_t i = 0; i < g_count; ++i) {
    const Function& f = g_functions[i];
    kprint("pci: {}:{}:{}.{} {x}:{x} class {x}{}{}\n", f.segment, f.bus, f.device, f.function,
           f.vendor, f.device_id, f.class_code, f.msi_cap != 0 ? " msi" : "",
           f.msix_cap != 0 ? " msix" : "");
  }
}

uint32_t function_count() { return g_count; }
const Function& function(uint32_t index) { return g_functions[index]; }

}  // namespace kernel::pci
