#include "pci.hpp"

#include <arch/x86_64/mmio.hpp>
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
constexpr uint8_t kCapVendor = 0x09;
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

namespace {

// Источник вторичный: pci-reference.md §4 (MSI capability).
constexpr uint16_t kMsiControl = 0x02;
constexpr uint16_t kMsiEnable = 1u << 0;
constexpr uint16_t kMsiMultipleEnableMask = 0x7u << 4;
constexpr uint16_t kMsi64Bit = 1u << 7;
constexpr uint16_t kMsiPerVectorMask = 1u << 8;

uint16_t msi_off(const Function& f, uint16_t rel) { return static_cast<uint16_t>(f.msi_cap + rel); }

}  // namespace

void set_command_bits(const Function& f, uint16_t set, uint16_t clear) {
  uint16_t c = read16(f, kRegCommand);
  write16(f, kRegCommand, static_cast<uint16_t>((c | set) & ~clear));
}

void msi_enable(const Function& f, uint32_t apic_id, uint8_t vector) {
  uint16_t ctrl = read16(f, msi_off(f, kMsiControl));
  bool wide = (ctrl & kMsi64Bit) != 0;
  write16(f, msi_off(f, kMsiControl), static_cast<uint16_t>(ctrl & ~kMsiEnable));
  // SDM §11.11.1: 0xFEE в битах 31:20, ID назначения в 19:12, RH = DM = 0.
  write32(f, msi_off(f, 0x04), 0xfee00000u | ((apic_id & 0xff) << 12));
  if (wide) write32(f, msi_off(f, 0x08), 0);
  // SDM §11.11.2: вектор в 7:0, режим Fixed (000), фронт.
  write16(f, msi_off(f, wide ? 0x0c : 0x08), vector);
  if ((ctrl & kMsiPerVectorMask) != 0) write32(f, msi_off(f, wide ? 0x10 : 0x0c), 0);
  ctrl = static_cast<uint16_t>((ctrl & ~kMsiMultipleEnableMask) | kMsiEnable);  // один вектор
  write16(f, msi_off(f, kMsiControl), ctrl);
  set_command_bits(f, kCommandIntxDisable, 0);
}

void msi_disable(const Function& f) {
  uint16_t ctrl = read16(f, msi_off(f, kMsiControl));
  write16(f, msi_off(f, kMsiControl), static_cast<uint16_t>(ctrl & ~kMsiEnable));
}

bool msi_set_mask(const Function& f, bool masked) {
  uint16_t ctrl = read16(f, msi_off(f, kMsiControl));
  if ((ctrl & kMsiPerVectorMask) == 0) return false;
  bool wide = (ctrl & kMsi64Bit) != 0;
  write32(f, msi_off(f, wide ? 0x10 : 0x0c), masked ? 1u : 0u);
  return true;
}

uint32_t function_count() { return g_count; }
const Function& function(uint32_t index) { return g_functions[index]; }

namespace {

// MSI-X: раскладка capability сверена с первоисточником — nvme-pcie-1.4.pdf,
// рис. 42–46 (MXID, MXC, MTAB, MPBA); формат записи таблицы — pci-reference.md §4
// (источник вторичный).
constexpr uint16_t kMsixControl = 0x02;
constexpr uint16_t kMsixTable = 0x04;
constexpr uint16_t kMsixPba = 0x08;
constexpr uint16_t kMsixEnable = 1u << 15;
constexpr uint16_t kMsixFunctionMask = 1u << 14;
constexpr uint16_t kMsixSizeMask = 0x7ff;  // размер таблицы − 1
constexpr uint32_t kMsixEntryMasked = 1u << 0;

uint16_t msix_off(const Function& f, uint16_t rel) {
  return static_cast<uint16_t>(f.msix_cap + rel);
}

uint64_t msix_entry(const Function& f, uint32_t entry) { return f.msix_table + entry * 16ull; }

}  // namespace

uint32_t msix_vectors(const Function& f) {
  if (f.msix_cap == 0) return 0;
  return (read16(f, msix_off(f, kMsixControl)) & kMsixSizeMask) + 1u;
}

bool msix_prepare(uint32_t index) {
  Function& f = g_functions[index];
  if (f.msix_cap == 0) return false;
  if (f.msix_table != 0) return true;
  uint32_t t = read32(f, msix_off(f, kMsixTable));
  const Bar& b = f.bars[t & 7];
  if (b.kind != BarKind::Mem32 && b.kind != BarKind::Mem64) return false;
  uint64_t size = uint64_t{msix_vectors(f)} * 16;
  auto r = mm::map_mmio(b.base + (t & ~7u), size);
  if (r.is_err()) return false;
  f.msix_table = r.value();
  return true;
}

void msix_program(const Function& f, uint32_t entry, uint32_t apic_id, uint8_t vector) {
  uint64_t e = msix_entry(f, entry);
  arch::x86_64::mmio_write32(e + 12, kMsixEntryMasked);
  // SDM §11.11.1–11.11.2: как у MSI.
  arch::x86_64::mmio_write32(e + 0, 0xfee00000u | ((apic_id & 0xff) << 12));
  arch::x86_64::mmio_write32(e + 4, 0);
  arch::x86_64::mmio_write32(e + 8, vector);
}

void msix_set_mask(const Function& f, uint32_t entry, bool masked) {
  arch::x86_64::mmio_write32(msix_entry(f, entry) + 12, masked ? kMsixEntryMasked : 0);
  (void)arch::x86_64::mmio_read32(msix_entry(f, entry) + 12);  // запись дошла до устройства
}

void msix_enable(const Function& f) {
  uint16_t ctrl = read16(f, msix_off(f, kMsixControl));
  write16(f, msix_off(f, kMsixControl),
          static_cast<uint16_t>((ctrl | kMsixEnable) & ~kMsixFunctionMask));
  set_command_bits(f, kCommandIntxDisable, 0);
}

void msix_disable(const Function& f) {
  uint16_t ctrl = read16(f, msix_off(f, kMsixControl));
  write16(f, msix_off(f, kMsixControl), static_cast<uint16_t>(ctrl & ~kMsixEnable));
}

bool msix_bar_pages(const Function& f, uint32_t bar, uint64_t* first, uint64_t* last) {
  if (f.msix_cap == 0) return false;
  bool found = false;
  uint64_t size = uint64_t{msix_vectors(f)} * 16;
  uint64_t pba_size = (uint64_t{msix_vectors(f)} + 63) / 64 * 8;
  const uint16_t regs[] = {kMsixTable, kMsixPba};
  for (uint16_t reg : regs) {
    uint32_t v = read32(f, msix_off(f, reg));
    if ((v & 7) != bar) continue;
    uint64_t start = (v & ~7u) / 4096;
    uint64_t end = ((v & ~7u) + (reg == kMsixTable ? size : pba_size) - 1) / 4096;
    if (!found || start < *first) *first = start;
    if (!found || end > *last) *last = end;
    found = true;
  }
  return found;
}

bool vendor_cap(const Function& f, uint32_t index, uint8_t* buf, uint32_t cap, uint32_t* len) {
  if ((read16(f, kRegStatus) & kStatusCapList) == 0) return false;
  uint8_t ptr = read8(f, kRegCapPtr) & 0xfc;
  for (int n = 0; ptr != 0 && n < kMaxCapWalk; ++n) {
    if (read8(f, ptr) == kCapVendor) {
      if (index == 0) {
        uint32_t l = read8(f, static_cast<uint16_t>(ptr + 2));
        if (l < 3 || ptr + l > 256) l = 3;  // неверная длина: только заголовок
        *len = l;
        for (uint32_t i = 0; i < l && i < cap; ++i) buf[i] = read8(f, static_cast<uint16_t>(ptr + i));
        return true;
      }
      --index;
    }
    ptr = read8(f, static_cast<uint16_t>(ptr + 1)) & 0xfc;
  }
  return false;
}

}  // namespace kernel::pci
