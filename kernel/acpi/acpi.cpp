#include "acpi.hpp"

#include <arch/x86_64/io.hpp>
#include <lib/acpi/parse.hpp>
#include <lib/acpi/pci_route.hpp>

#include "../core/log.hpp"
#include "../mm/heap.hpp"
#include "../mm/layout.hpp"
#include "../mm/vmm.hpp"
#include "../pci/pci.hpp"
#include "../platform/platform.hpp"

namespace kernel::acpi {

namespace {

namespace aml = lib::acpi::aml;
using lib::core::StringView;

constinit aml::Interpreter g_aml;
constinit bool g_ready = false;

struct Route {
  bool valid;
  Intx line;
};
constinit Route g_routes[pci::kMaxFunctions] = {};

void* alloc(void*, size_t n) {
  auto r = mm::kmalloc(n);
  return r.is_ok() ? r.value() : nullptr;
}

// Память для SystemMemory-регионов: отображение по страницам с кешем.
struct Mapped {
  uint64_t page;
  uint64_t va;
};
constinit Mapped g_maps[32] = {};
constinit uint32_t g_map_count = 0;

volatile uint8_t* mem(uint64_t phys) {
  uint64_t page = phys & ~uint64_t{0xfff};
  for (uint32_t i = 0; i < g_map_count; ++i) {
    if (g_maps[i].page == page) return reinterpret_cast<volatile uint8_t*>(g_maps[i].va + (phys & 0xfff));
  }
  uint64_t p = 0, f = 0;
  if (mm::translate(mm::kHhdmBase + phys, &p, &f)) return mm::phys_to_virt<volatile uint8_t>(phys);
  auto m = mm::map_mmio(page, 4096);
  if (m.is_err()) return nullptr;
  if (g_map_count < 32) g_maps[g_map_count++] = {page, m.value()};
  return reinterpret_cast<volatile uint8_t*>(m.value() + (phys & 0xfff));
}

uint16_t inw(uint16_t port) {
  uint16_t v;
  asm volatile("inw %1, %0" : "=a"(v) : "Nd"(port) : "memory");
  return v;
}
uint32_t inl(uint16_t port) {
  uint32_t v;
  asm volatile("inl %1, %0" : "=a"(v) : "Nd"(port) : "memory");
  return v;
}
void outw(uint16_t port, uint16_t v) { asm volatile("outw %0, %1" : : "a"(v), "Nd"(port) : "memory"); }

const pci::Function* pci_find(const aml::PciAddress& a) {
  for (uint32_t i = 0; i < pci::function_count(); ++i) {
    const pci::Function& f = pci::function(i);
    if (f.segment == a.segment && f.bus == a.bus && f.device == a.device && f.function == a.function) return &f;
  }
  return nullptr;
}

uint64_t host_read(void*, aml::Space s, uint64_t addr, uint32_t width, const aml::PciAddress* pci) {
  switch (s) {
    case aml::Space::SystemMemory: {
      volatile uint8_t* p = mem(addr);
      if (p == nullptr) return 0;
      switch (width) {
        case 8: return *p;
        case 16: return *reinterpret_cast<volatile uint16_t*>(p);
        case 32: return *reinterpret_cast<volatile uint32_t*>(p);
        default: return *reinterpret_cast<volatile uint64_t*>(p);
      }
    }
    case aml::Space::SystemIO: {
      auto port = static_cast<uint16_t>(addr);
      if (width == 8) return arch::x86_64::inb(port);
      if (width == 16) return inw(port);
      return inl(port);
    }
    case aml::Space::PciConfig: {
      const pci::Function* f = pci != nullptr ? pci_find(*pci) : nullptr;
      if (f == nullptr) return ~uint64_t{0};  // нет функции — как чтение пустого слота
      auto off = static_cast<uint16_t>(addr);
      if (width == 8) return pci::read8(*f, off);
      if (width == 16) return pci::read16(*f, off);
      return pci::read32(*f, off);
    }
    default:
      return 0;
  }
}

void host_write(void*, aml::Space s, uint64_t addr, uint32_t width, uint64_t v, const aml::PciAddress* pci) {
  switch (s) {
    case aml::Space::SystemMemory: {
      volatile uint8_t* p = mem(addr);
      if (p == nullptr) return;
      switch (width) {
        case 8: *p = static_cast<uint8_t>(v); break;
        case 16: *reinterpret_cast<volatile uint16_t*>(p) = static_cast<uint16_t>(v); break;
        case 32: *reinterpret_cast<volatile uint32_t*>(p) = static_cast<uint32_t>(v); break;
        default: *reinterpret_cast<volatile uint64_t*>(p) = v; break;
      }
      return;
    }
    case aml::Space::SystemIO: {
      auto port = static_cast<uint16_t>(addr);
      if (width == 8) {
        arch::x86_64::outb(port, static_cast<uint8_t>(v));
      } else if (width == 16) {
        outw(port, static_cast<uint16_t>(v));
      } else {
        arch::x86_64::outl(port, static_cast<uint32_t>(v));
      }
      return;
    }
    case aml::Space::PciConfig: {
      const pci::Function* f = pci != nullptr ? pci_find(*pci) : nullptr;
      if (f == nullptr) return;
      auto off = static_cast<uint16_t>(addr);
      if (width == 16) {
        pci::write16(*f, off, static_cast<uint16_t>(v));
      } else if (width == 32) {
        pci::write32(*f, off, static_cast<uint32_t>(v));
      } else {
        // Байт — через слово (запись байтом в pci.hpp нет).
        uint16_t word = pci::read16(*f, static_cast<uint16_t>(off & ~1u));
        uint32_t shift = (off & 1u) * 8;
        word = static_cast<uint16_t>((word & ~(0xffu << shift)) | ((v & 0xff) << shift));
        pci::write16(*f, static_cast<uint16_t>(off & ~1u), word);
      }
      return;
    }
    default:
      return;
  }
}

void host_log(void*, const char* m) { kprint("acpi: aml: {}\n", StringView(m)); }
void host_sleep(void*, uint64_t ms) { platform::ref_clock().wait_ns(ms * 1'000'000); }
void host_stall(void*, uint64_t us) { platform::ref_clock().wait_ns(us * 1000); }

void gas_write(const lib::acpi::Gas& g, uint64_t value, uint32_t width) {
  if (g.address == 0) return;
  host_write(nullptr, g.space == 0 ? aml::Space::SystemMemory : aml::Space::SystemIO, g.address, width, value, nullptr);
}

uint64_t gas_read(const lib::acpi::Gas& g, uint32_t width) {
  if (g.address == 0) return 0;
  return host_read(nullptr, g.space == 0 ? aml::Space::SystemMemory : aml::Space::SystemIO, g.address, width, nullptr);
}

// MPS INTI (ACPI 6.5 табл. 5.26): полярность биты 1:0, режим биты 3:2; 0 — как у шины.
void isa_to_gsi(uint32_t irq, Intx* out) {
  out->gsi = irq;
  const auto& m = platform::madt();
  for (size_t i = 0; i < m.override_count; ++i) {
    if (m.overrides[i].source != irq) continue;
    out->gsi = m.overrides[i].gsi;
    uint16_t f = m.overrides[i].flags;
    if ((f & lib::acpi::kIntiPolarityMask) != 0) out->active_low = (f & lib::acpi::kIntiPolarityMask) == lib::acpi::kIntiPolarityLow;
    if ((f & lib::acpi::kIntiTriggerMask) != 0) out->level = (f & lib::acpi::kIntiTriggerMask) == lib::acpi::kIntiTriggerLevel;
  }
}

// Маршруты для функций корневой шины без MSI/MSI-X (за мостами PCI-PCI — пока нет).
void route_pci() {
  aml::Node* bridge = lib::acpi::find_root_bridge(g_aml);
  if (bridge == nullptr) {
    kprint("acpi: no PCI root bridge in the namespace\n");
    return;
  }
  uint64_t bbn = 0;
  if (aml::Node* n = g_aml.find("_BBN", bridge); n != nullptr && n->parent == bridge) {
    aml::Value v{};
    if (g_aml.evaluate(n, nullptr, 0, &v) == aml::Error::Ok && v.type == aml::Type::Integer) bbn = v.integer;
  }
  for (uint32_t i = 0; i < pci::function_count(); ++i) {
    const pci::Function& f = pci::function(i);
    if (f.bus != bbn || f.msi_cap != 0 || pci::msix_vectors(f) != 0) continue;
    uint8_t pin = pci::read8(f, 0x3d);  // Interrupt Pin: 1 — INTA … 4 — INTD
    if (pin < 1 || pin > 4) continue;
    lib::acpi::IntxRoute r{};
    if (!lib::acpi::route_intx(g_aml, bridge, f.device, static_cast<uint8_t>(pin - 1), &r)) continue;
    Intx line{r.irq, r.level, r.active_low};
    if (r.isa) isa_to_gsi(r.irq, &line);
    g_routes[i] = {true, line};
    kprint("acpi: pci {}:{}.{} INT{} -> GSI {} {} {}\n", f.bus, f.device, f.function, static_cast<char>('A' + pin - 1),
           line.gsi, line.level ? "level" : "edge", line.active_low ? "low" : "high");
  }
}

}  // namespace

bool intx(uint32_t pci_index, Intx* out) {
  if (pci_index >= pci::kMaxFunctions || !g_routes[pci_index].valid) return false;
  *out = g_routes[pci_index].line;
  return true;
}

void init() {
  const auto& fadt = platform::fadt();
  if (fadt.dsdt_address == 0) {
    kprint("acpi: no DSDT\n");
    return;
  }
  g_aml.init({&alloc, nullptr}, {&alloc, nullptr},
             {nullptr, &host_read, &host_write, &host_log, &host_sleep, &host_stall});
  const auto* head = static_cast<const lib::acpi::SdtHeader*>(platform::map_acpi_table(fadt.dsdt_address, 36));
  const auto* dsdt = head != nullptr ? platform::map_acpi_table(fadt.dsdt_address, head->length) : nullptr;
  if (dsdt == nullptr) {
    kprint("acpi: cannot map DSDT\n");
    return;
  }
  auto e = g_aml.load_table(static_cast<const uint8_t*>(dsdt), head->length);
  if (e != aml::Error::Ok) {
    kprint("acpi: DSDT: {} at {x}\n", aml::error_name(e), g_aml.error_offset());
    return;
  }
  uint32_t ssdts = 0;
  for (uint32_t i = 0;; ++i) {
    const lib::acpi::SdtHeader* t = platform::find_acpi_table("SSDT", i);
    if (t == nullptr) break;
    e = g_aml.load_table(reinterpret_cast<const uint8_t*>(t), t->length);
    if (e != aml::Error::Ok) kprint("acpi: SSDT {}: {} at {x}\n", i, aml::error_name(e), g_aml.error_offset());
    ++ssdts;
  }
  // §5.8.1: \_PIC(1) — прерывания через I/O APIC.
  if (aml::Node* pic = g_aml.find("\\_PIC")) {
    aml::Value arg{};
    arg.type = aml::Type::Integer;
    arg.integer = 1;
    aml::Value r{};
    (void)g_aml.evaluate(pic, &arg, 1, &r);
  }
  g_ready = true;
  route_pci();
  kprint("acpi: AML: {} nodes, {} methods, {} SSDT, {}-bit integers\n", g_aml.node_count(), g_aml.method_count(),
         ssdts, g_aml.integer_width());
}

bool ready() { return g_ready; }
aml::Interpreter& interpreter() { return g_aml; }

void power_off() {
  if (!g_ready) {
    kprint("acpi: power off: no AML\n");
    return;
  }
  aml::Value s5{};
  if (g_aml.evaluate("\\_S5", &s5) != aml::Error::Ok || s5.type != aml::Type::Package || s5.size < 1) {
    kprint("acpi: power off: no _S5\n");
    return;
  }
  uint64_t typ_a = s5.items[0].type == aml::Type::Integer ? s5.items[0].integer : 0;
  uint64_t typ_b = s5.size > 1 && s5.items[1].type == aml::Type::Integer ? s5.items[1].integer : typ_a;
  // §7.4.2.1: _PTS(5) перед переходом.
  if (aml::Node* pts = g_aml.find("\\_PTS")) {
    aml::Value arg{};
    arg.type = aml::Type::Integer;
    arg.integer = 5;
    aml::Value r{};
    (void)g_aml.evaluate(pts, &arg, 1, &r);
  }
  const auto& fadt = platform::fadt();
  // SCI_EN (бит 0 PM1_CNT) не установлен — регистры у SMI: ACPI_ENABLE в SMI_CMD.
  if ((gas_read(fadt.pm1a_cnt, 16) & 1) == 0 && fadt.smi_cmd != 0 && fadt.acpi_enable != 0) {
    arch::x86_64::outb(static_cast<uint16_t>(fadt.smi_cmd), fadt.acpi_enable);
    for (int i = 0; i < 300 && (gas_read(fadt.pm1a_cnt, 16) & 1) == 0; ++i) host_sleep(nullptr, 10);
  }
  kprint("acpi: entering S5\n");
  // Табл. 4.13: SLP_TYPx — биты 12:10, SLP_EN — бит 13.
  uint64_t a = (gas_read(fadt.pm1a_cnt, 16) & ~uint64_t{0x3c00}) | ((typ_a & 7) << 10) | (1u << 13);
  uint64_t b = (gas_read(fadt.pm1b_cnt, 16) & ~uint64_t{0x3c00}) | ((typ_b & 7) << 10) | (1u << 13);
  gas_write(fadt.pm1b_cnt, b, 16);
  gas_write(fadt.pm1a_cnt, a, 16);
  host_sleep(nullptr, 1000);
  kprint("acpi: S5 did not happen\n");
}

void reboot() {
  const auto& fadt = platform::fadt();
  if ((fadt.flags & lib::acpi::kFadtResetRegSup) == 0 || fadt.reset_reg.address == 0) {
    kprint("acpi: no reset register\n");
    return;
  }
  kprint("acpi: reset\n");
  gas_write(fadt.reset_reg, fadt.reset_value, 8);
  host_sleep(nullptr, 1000);
}

}  // namespace kernel::acpi
