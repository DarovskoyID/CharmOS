#include "iommu.hpp"

#include <arch/x86_64/cpu.hpp>
#include <arch/x86_64/idt.hpp>
#include <arch/x86_64/mmio.hpp>
#include <arch/x86_64/paging.hpp>
#include <lib/acpi/parse.hpp>

#include "../core/log.hpp"
#include "../core/panic.hpp"
#include "../mm/heap.hpp"
#include "../mm/layout.hpp"
#include "../mm/pmm.hpp"
#include "../mm/vmm.hpp"
#include "../platform/platform.hpp"
#include "../core/cmdline.hpp"
#include "../smp/cpu.hpp"
#include "../sync/spinlock.hpp"
#include "pci.hpp"

namespace kernel::pci::iommu {

namespace {

using arch::x86_64::kPageSize;
using lib::core::Result;
using arch::x86_64::mmio_read32;
using arch::x86_64::mmio_read64;
using arch::x86_64::mmio_write32;
using arch::x86_64::mmio_write64;

// --- регистры (vtd-5.2 §11.4, табл. 11.4) ---------------------------------------------
constexpr uint64_t kRegCap = 0x08;
constexpr uint64_t kRegEcap = 0x10;
constexpr uint64_t kRegGcmd = 0x18;
constexpr uint64_t kRegGsts = 0x1c;
constexpr uint64_t kRegRtaddr = 0x20;
constexpr uint64_t kRegCcmd = 0x28;
constexpr uint64_t kRegFsts = 0x34;
constexpr uint64_t kRegFectl = 0x38;
constexpr uint64_t kRegFedata = 0x3c;
constexpr uint64_t kRegFeaddr = 0x40;
constexpr uint64_t kRegFeuaddr = 0x44;

// CAP_REG, §11.4.2.
uint32_t cap_nd(uint64_t c) { return c & 7; }               // 2:0 — домены: 2^(4+2·ND)
constexpr uint64_t kCapRwbf = 1ull << 4;                     // нужен сброс буфера записи
constexpr uint64_t kCapCm = 1ull << 7;                       // Caching Mode
uint32_t cap_sagaw(uint64_t c) { return (c >> 8) & 0x1f; }  // 12:8
uint32_t cap_fro(uint64_t c) { return (c >> 24) & 0x3ff; }  // 33:24, в 16-байтных единицах
uint32_t cap_nfr(uint64_t c) { return ((c >> 40) & 0xff) + 1; }  // 47:40, N−1
constexpr uint64_t kCapDwd = 1ull << 54;
constexpr uint64_t kCapDrd = 1ull << 55;
constexpr uint32_t kSagaw39 = 1u << 1;  // 3 уровня
constexpr uint32_t kSagaw48 = 1u << 2;  // 4 уровня

// ECAP_REG, §11.4.3.
constexpr uint64_t kEcapCoherent = 1ull << 0;                 // C: обход таблиц когерентен
uint32_t ecap_iro(uint64_t e) { return (e >> 8) & 0x3ff; }  // 17:8, в 16-байтных единицах

// GCMD_REG / GSTS_REG, §11.4.4–11.4.5.
constexpr uint32_t kGcmdTe = 1u << 31;
constexpr uint32_t kGcmdSrtp = 1u << 30;
constexpr uint32_t kGcmdWbf = 1u << 27;
constexpr uint32_t kGstsTes = 1u << 31;
constexpr uint32_t kGstsRtps = 1u << 30;
constexpr uint32_t kGstsWbfs = 1u << 27;
// Одноразовые биты статуса, которые нельзя переносить в команду (§11.4.4,
// рекомендация: «Status = GSTS & 0x96FFFFFF»).
constexpr uint32_t kGstsPersistentMask = 0x96ffffffu;

// CCMD_REG, §11.4.6.1.
constexpr uint64_t kCcmdIcc = 1ull << 63;
constexpr uint64_t kCcmdGlobal = 1ull << 61;  // CIRG 62:61 = 01

// IOTLB_REG (IVA_REG + 8), §11.4.6.2–11.4.6.3.
constexpr uint64_t kIotlbIvt = 1ull << 63;
constexpr uint64_t kIotlbGlobal = 1ull << 60;  // IIRG 61:60 = 01
constexpr uint64_t kIotlbDomain = 2ull << 60;  // 10
constexpr uint64_t kIotlbDr = 1ull << 49;
constexpr uint64_t kIotlbDw = 1ull << 48;
uint64_t iotlb_did(uint16_t did) { return uint64_t{did} << 32; }  // 47:32

// FSTS_REG, FECTL_REG, §11.4.7.
constexpr uint32_t kFstsPfo = 1u << 0;
uint32_t fsts_fri(uint32_t s) { return (s >> 8) & 0xff; }
constexpr uint32_t kFectlIm = 1u << 31;

// FRCD_REG (128 бит), §11.4.7.4: старшие 64 бита.
constexpr uint64_t kFrcdFault = 1ull << 63;  // бит 127, RW1CS

// --- таблицы (§9) -------------------------------------------------------------------------
// Root Entry (§9.1): младшие 64 бита — CTP | P. Context Entry (§9.3): младшие —
// SSPTPTR | TT(3:2) | FPD(1) | P(0), старшие — AW(66:64) | DID(87:72).
struct alignas(16) Entry128 {
  uint64_t lo;
  uint64_t hi;
};
static_assert(sizeof(Entry128) == 16);

constexpr uint64_t kPresent = 1;
constexpr uint64_t kAddrMask = 0x000ffffffffff000ull;
// Записи второго уровня (§9.8, табл. 41–47): R(0), W(1), адрес 12+.
constexpr uint64_t kSsRead = 1ull << 0;
constexpr uint64_t kSsWrite = 1ull << 1;

constexpr uint64_t kIovaBase = 0x100000;        // 0 и первый мегабайт не выдаются
constexpr uint64_t kIovaLimit = 1ull << 32;

struct Unit {
  uint64_t regs;
  uint64_t cap;
  uint64_t ecap;
  uint16_t segment;
  bool include_all;
  bool coherent;
  uint64_t root_phys;
  uint64_t context_phys[256];  // по шине, 0 — таблицы нет
};

struct DomainImpl {
  uint16_t id;
  uint64_t root_phys;
  uint64_t next_iova;
};

constinit sync::Spinlock g_lock{sync::LockRank::Iommu, "iommu"};
constinit Unit g_units[lib::acpi::DmarInfo::kMaxUnits] = {};
constinit uint32_t g_unit_count = 0;
constinit bool g_active = false;
constinit bool g_cmdline_off = false;
constinit uint32_t g_levels = 0;     // уровней таблиц второго уровня
constinit uint64_t g_aw = 0;         // поле AW контекста
constinit int8_t g_unit_of[kMaxFunctions] = {};  // −1 — не обслуживается
constinit DomainImpl* g_owner[kMaxFunctions] = {};
constexpr uint32_t kMaxDomainIds = 1024;
constinit uint64_t g_ids[kMaxDomainIds / 64] = {};
constinit uint32_t g_id_limit = 0;
constinit FaultHandler g_fault_handler = nullptr;
constinit uint64_t g_faults = 0;

// Обход таблиц у всех блоков должен видеть запись: при некогерентном обходе
// (ECAP.C = 0) строку кэша надо вытолкнуть (§3.10, табл. 5–6).
constinit bool g_any_noncoherent = false;

void publish(const volatile void* p) {
  if (g_any_noncoherent) {
    arch::x86_64::clflush(p);
    arch::x86_64::mfence();
  } else {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
  }
}

void store(volatile uint64_t* p, uint64_t v) {
  __atomic_store_n(p, v, __ATOMIC_RELEASE);
  publish(p);
}

Result<uint64_t, int> zeroed_frame() {
  auto f = mm::alloc_frames();
  if (f.is_err()) return Result<uint64_t, int>::err(0);
  __builtin_memset(mm::phys_to_virt<void>(f.value()), 0, kPageSize);
  // Таблица видна IOMMU, как только на неё сослались: нули — в память.
  if (g_any_noncoherent) {
    for (uint64_t off = 0; off < kPageSize; off += 64) {
      arch::x86_64::clflush(mm::phys_to_virt<uint8_t>(f.value()) + off);
    }
    arch::x86_64::mfence();
  }
  return Result<uint64_t, int>::ok(f.value());
}

uint64_t* table(uint64_t phys) { return mm::phys_to_virt<uint64_t>(phys); }

void wait_clear64(const Unit& u, uint64_t reg, uint64_t bit, const char* what) {
  for (uint64_t i = 0; i < 100'000'000; ++i) {
    if ((mmio_read64(u.regs + reg) & bit) == 0) return;
    arch::x86_64::pause();
  }
  panic("iommu: {} did not complete", what);
}

void wait_status(const Unit& u, uint32_t bit, bool set, const char* what) {
  for (uint64_t i = 0; i < 100'000'000; ++i) {
    if (((mmio_read32(u.regs + kRegGsts) & bit) != 0) == set) return;
    arch::x86_64::pause();
  }
  panic("iommu: {} did not complete", what);
}

// Команда GCMD (§11.4.4): текущие постоянные биты + новый.
void gcmd(const Unit& u, uint32_t set, uint32_t clear) {
  uint32_t s = mmio_read32(u.regs + kRegGsts) & kGstsPersistentMask;
  mmio_write32(u.regs + kRegGcmd, (s | set) & ~clear);
}

void flush_write_buffer(const Unit& u) {
  if ((u.cap & kCapRwbf) == 0) return;
  gcmd(u, kGcmdWbf, 0);
  wait_status(u, kGstsWbfs, false, "write buffer flush");
}

void invalidate_context(const Unit& u) {
  flush_write_buffer(u);
  mmio_write64(u.regs + kRegCcmd, kCcmdIcc | kCcmdGlobal);
  wait_clear64(u, kRegCcmd, kCcmdIcc, "context invalidation");
}

void invalidate_iotlb(const Unit& u, uint64_t granularity) {
  flush_write_buffer(u);
  uint64_t reg = uint64_t{ecap_iro(u.ecap)} * 16 + 8;
  uint64_t cmd = kIotlbIvt | granularity;
  if ((u.cap & kCapDrd) != 0) cmd |= kIotlbDr;
  if ((u.cap & kCapDwd) != 0) cmd |= kIotlbDw;
  mmio_write64(u.regs + reg, cmd);
  wait_clear64(u, reg, kIotlbIvt, "IOTLB invalidation");
}

uint16_t alloc_id() {
  // При Caching Mode идентификатор 0 зарезервирован (§9.3) — не выдаём его вовсе.
  for (uint32_t id = 1; id < g_id_limit; ++id) {
    uint64_t bit = 1ull << (id % 64);
    if ((g_ids[id / 64] & bit) == 0) {
      g_ids[id / 64] |= bit;
      return static_cast<uint16_t>(id);
    }
  }
  return 0;
}

void free_id(uint16_t id) { g_ids[id / 64] &= ~(1ull << (id % 64)); }

volatile Entry128* context_entry(Unit& u, const Function& f, bool create) {
  if (u.context_phys[f.bus] == 0) {
    if (!create) return nullptr;
    auto ctx = zeroed_frame();
    if (ctx.is_err()) return nullptr;
    u.context_phys[f.bus] = ctx.value();
    auto* root = mm::phys_to_virt<volatile Entry128>(u.root_phys) + f.bus;
    store(&root->lo, ctx.value() | kPresent);
  }
  auto* ctx = mm::phys_to_virt<volatile Entry128>(u.context_phys[f.bus]);
  return ctx + (uint32_t{f.device} << 3 | f.function);
}

// Снимает привязку функции (под g_lock).
void detach_locked(uint32_t pci_index) {
  DomainImpl* d = g_owner[pci_index];
  if (d == nullptr) return;
  Unit& u = g_units[g_unit_of[pci_index]];
  volatile Entry128* e = context_entry(u, function(pci_index), false);
  if (e != nullptr) {
    store(&e->lo, 0);  // P = 0 первым
    store(&e->hi, 0);
  }
  invalidate_context(u);
  invalidate_iotlb(u, kIotlbDomain | iotlb_did(d->id));
  g_owner[pci_index] = nullptr;
}

void free_tree(uint64_t phys, uint32_t level) {
  if (level > 1) {
    uint64_t* t = table(phys);
    for (uint32_t i = 0; i < 512; ++i) {
      if ((t[i] & (kSsRead | kSsWrite)) != 0) free_tree(t[i] & kAddrMask, level - 1);
    }
  }
  mm::free_frames(phys);
}

// Запись последнего уровня для iova; при create строит недостающие таблицы.
volatile uint64_t* leaf(DomainImpl* d, uint64_t iova, bool create) {
  uint64_t phys = d->root_phys;
  for (uint32_t level = g_levels; level > 1; --level) {
    volatile uint64_t* e = table(phys) + ((iova >> (12 + 9 * (level - 1))) & 511);
    uint64_t v = *e;
    if ((v & (kSsRead | kSsWrite)) == 0) {
      if (!create) return nullptr;
      auto t = zeroed_frame();
      if (t.is_err()) return nullptr;
      // Промежуточные уровни — R|W: права ограничивает запись последнего уровня.
      v = t.value() | kSsRead | kSsWrite;
      store(e, v);
    }
    phys = v & kAddrMask;
  }
  return table(phys) + ((iova >> 12) & 511);
}

void invalidate_domain(DomainImpl* d) {
  for (uint32_t i = 0; i < g_unit_count; ++i) {
    invalidate_iotlb(g_units[i], kIotlbDomain | iotlb_did(d->id));
  }
}

const char* fault_reason(uint8_t r) {
  // §7.1.1, табл. 30 (LRT.2, LCT.2, LGN.1–LGN.3).
  switch (r) {
    case 0x01: return "root entry not present";
    case 0x02: return "context entry not present";
    case 0x04: return "address beyond domain width";
    case 0x05: return "write not permitted";
    case 0x06: return "read not permitted";
    default: return "translation fault";
  }
}

int32_t function_by_sid(uint16_t segment, uint16_t sid) {
  for (uint32_t i = 0; i < function_count(); ++i) {
    const Function& f = function(i);
    uint16_t s = static_cast<uint16_t>(uint32_t{f.bus} << 8 | uint32_t{f.device} << 3 | f.function);
    if (f.segment == segment && s == sid) return static_cast<int32_t>(i);
  }
  return -1;
}

void fault_interrupt(arch::x86_64::InterruptFrame&) {
  struct Record {
    int32_t pci_index;
    uint64_t address;
    uint8_t reason;
  };
  Record recs[16];
  uint32_t n = 0;
  {
    sync::SpinGuard guard(g_lock);
    for (uint32_t ui = 0; ui < g_unit_count; ++ui) {
      Unit& u = g_units[ui];
      uint32_t fsts = mmio_read32(u.regs + kRegFsts);
      uint32_t nfr = cap_nfr(u.cap);
      uint64_t base = u.regs + uint64_t{cap_fro(u.cap)} * 16;
      for (uint32_t k = 0, i = fsts_fri(fsts); k < nfr; ++k, i = (i + 1) % nfr) {
        uint64_t hi = mmio_read64(base + i * 16 + 8);
        if ((hi & kFrcdFault) == 0) break;
        uint64_t lo = mmio_read64(base + i * 16);
        mmio_write64(base + i * 16 + 8, kFrcdFault);  // RW1C
        ++g_faults;
        if (n < 16) {
          recs[n++] = {function_by_sid(u.segment, static_cast<uint16_t>(hi & 0xffff)),
                       lo & ~0xfffull, static_cast<uint8_t>((hi >> 32) & 0xff)};
        }
      }
      mmio_write32(u.regs + kRegFsts, kFstsPfo);  // RW1C
    }
  }
  platform::local_apic().eoi();
  for (uint32_t i = 0; i < n; ++i) {
    if (recs[i].pci_index < 0) {
      kprint("iommu: fault from unknown requester, {} at {x}\n", fault_reason(recs[i].reason),
             recs[i].address);
      continue;
    }
    const Function& f = function(static_cast<uint32_t>(recs[i].pci_index));
    kprint("iommu: fault {}:{}.{}: {} at {x}\n", f.bus, f.device, f.function,
           fault_reason(recs[i].reason), recs[i].address);
    if (g_fault_handler != nullptr) {
      g_fault_handler(static_cast<uint32_t>(recs[i].pci_index), recs[i].address, recs[i].reason);
    }
  }
}

const void* map_acpi_table(void*, uint64_t phys, size_t size) {
  auto r = mm::map_mmio(phys, size);
  return r.is_ok() ? reinterpret_cast<const void*>(r.value()) : nullptr;
}

bool init_unit(Unit& u, uint8_t vector) {
  // Окно регистров: до конца блока IOTLB и записей об ошибках.
  auto first = mm::map_mmio(u.regs, kPageSize);
  if (first.is_err()) return false;
  u.cap = mmio_read64(first.value() + kRegCap);
  u.ecap = mmio_read64(first.value() + kRegEcap);
  uint64_t end = uint64_t{ecap_iro(u.ecap)} * 16 + 16;
  uint64_t frd_end = (uint64_t{cap_fro(u.cap)} + cap_nfr(u.cap)) * 16;
  if (frd_end > end) end = frd_end;
  uint64_t phys = u.regs;
  if (end > kPageSize) {
    auto all = mm::map_mmio(phys, (end + kPageSize - 1) & ~(kPageSize - 1));
    if (all.is_err()) return false;
    u.regs = all.value();
  } else {
    u.regs = first.value();
  }
  u.coherent = (u.ecap & kEcapCoherent) != 0;
  if (!u.coherent) g_any_noncoherent = true;

  // Прошивка могла оставить трансляцию включённой: выключить до смены таблиц.
  if ((mmio_read32(u.regs + kRegGsts) & kGstsTes) != 0) {
    gcmd(u, 0, kGcmdTe);
    wait_status(u, kGstsTes, false, "translation disable");
  }
  auto root = zeroed_frame();
  if (root.is_err()) return false;
  u.root_phys = root.value();
  mmio_write64(u.regs + kRegRtaddr, u.root_phys);  // TTM = 00: legacy
  gcmd(u, kGcmdSrtp, 0);
  wait_status(u, kGstsRtps, true, "root table pointer");
  invalidate_context(u);
  invalidate_iotlb(u, kIotlbGlobal);

  // Ошибки — прерыванием в формате MSI (§11.4.7.2–11.4.7.4; адрес и данные —
  // как у MSI устройств, SDM §11.11).
  mmio_write32(u.regs + kRegFedata, vector);
  mmio_write32(u.regs + kRegFeaddr, 0xfee00000u | ((smp::cpu(0).apic_id & 0xff) << 12));
  mmio_write32(u.regs + kRegFeuaddr, 0);
  mmio_write32(u.regs + kRegFectl, 0);  // IM = 0
  (void)kFectlIm;

  gcmd(u, kGcmdTe, 0);
  wait_status(u, kGstsTes, true, "translation enable");
  kprint("iommu: unit at {x} segment {} cap={x} ecap={x}{}\n", phys, u.segment, u.cap, u.ecap,
         u.coherent ? "" : " non-coherent");
  return true;
}

}  // namespace

void init(const abi::boot::BootInfo& info, const Cmdline& cmdline) {
  for (uint32_t i = 0; i < kMaxFunctions; ++i) g_unit_of[i] = -1;
  if (cmdline.get("iommu") == lib::core::StringView("off")) {
    g_cmdline_off = true;
    kprint("iommu: off by command line, DMA without isolation\n");
    return;
  }
  const void* rsdp = map_acpi_table(nullptr, info.acpi_rsdp, sizeof(lib::acpi::Rsdp));
  auto xsdt_phys = lib::acpi::parse_rsdp(rsdp);
  if (xsdt_phys.is_err()) panic("iommu: RSDP");
  const auto* head = static_cast<const lib::acpi::SdtHeader*>(
      map_acpi_table(nullptr, xsdt_phys.value(), sizeof(lib::acpi::SdtHeader)));
  const auto* xsdt = static_cast<const lib::acpi::SdtHeader*>(
      map_acpi_table(nullptr, xsdt_phys.value(), head->length));
  auto dmar = lib::acpi::find_table(xsdt, "DMAR", 0, &map_acpi_table, nullptr);
  if (dmar.is_err()) {
    kprint("iommu: no DMAR, DMA unavailable\n");
    return;
  }
  lib::acpi::DmarInfo m;
  if (auto r = lib::acpi::parse_dmar(dmar.value(), &m); r.is_err()) {
    kprint("iommu: bad DMAR: {}\n", lib::acpi::error_name(r.error()));
    return;
  }
  if (m.count == 0) {
    kprint("iommu: DMAR without remapping units\n");
    return;
  }

  // Общая глубина таблиц для всех блоков: домен один на все.
  uint32_t sagaw = 0x1f;
  g_id_limit = kMaxDomainIds;
  for (size_t i = 0; i < m.count; ++i) {
    auto probe = mm::map_mmio(m.units[i].register_base, kPageSize);
    if (probe.is_err()) panic("iommu: cannot map registers");
    uint64_t cap = mmio_read64(probe.value() + kRegCap);
    sagaw &= cap_sagaw(cap);
    uint32_t ids = 1u << (4 + 2 * cap_nd(cap));
    if (ids < g_id_limit) g_id_limit = ids;
    (void)kCapCm;
  }
  if ((sagaw & kSagaw39) != 0) {
    g_levels = 3;
    g_aw = 1;
  } else if ((sagaw & kSagaw48) != 0) {
    g_levels = 4;
    g_aw = 2;
  } else {
    kprint("iommu: no common 39/48-bit address width, DMA unavailable\n");
    return;
  }

  uint8_t vector = platform::alloc_device_vector(&fault_interrupt);
  if (vector == 0) panic("iommu: no vector for fault events");
  for (size_t i = 0; i < m.count; ++i) {
    Unit& u = g_units[g_unit_count];
    u.regs = m.units[i].register_base;
    u.segment = m.units[i].segment;
    u.include_all = m.units[i].include_all;
    if (!init_unit(u, vector)) panic("iommu: cannot initialize unit");
    ++g_unit_count;
    if (m.units[i].partial_scope) {
      kprint("iommu: unit {x}: bridge scopes not supported, devices behind them get no DMA\n",
             m.units[i].register_base);
    }
    for (uint32_t fi = 0; fi < function_count(); ++fi) {
      const Function& f = function(fi);
      if (f.segment != u.segment) continue;
      for (size_t k = 0; k < m.units[i].endpoint_count; ++k) {
        const lib::acpi::DmarEndpoint& e = m.units[i].endpoints[k];
        if (e.bus == f.bus && e.device == f.device && e.function == f.function) {
          g_unit_of[fi] = static_cast<int8_t>(g_unit_count - 1);
        }
      }
    }
  }
  // INCLUDE_PCI_ALL — всё, что не попало в явные области (§8.3).
  for (uint32_t ui = 0; ui < g_unit_count; ++ui) {
    if (!g_units[ui].include_all) continue;
    for (uint32_t fi = 0; fi < function_count(); ++fi) {
      if (g_unit_of[fi] < 0 && function(fi).segment == g_units[ui].segment) {
        g_unit_of[fi] = static_cast<int8_t>(ui);
      }
    }
  }
  g_active = true;
  kprint("iommu: VT-d active, {} units, {}-level tables, {} domain ids\n", g_unit_count, g_levels,
         g_id_limit);
}

bool active() { return g_active; }
bool disabled_by_cmdline() { return g_cmdline_off; }

struct Domain : DomainImpl {};

Domain* domain_create() {
  if (!g_active) return nullptr;
  auto mem = mm::kmalloc(sizeof(Domain));
  if (mem.is_err()) return nullptr;
  auto* d = static_cast<Domain*>(mem.value());
  auto root = zeroed_frame();
  if (root.is_err()) {
    mm::kfree(d);
    return nullptr;
  }
  sync::SpinGuard guard(g_lock);
  d->id = alloc_id();
  if (d->id == 0) {
    mm::free_frames(root.value());
    mm::kfree(d);
    return nullptr;
  }
  d->root_phys = root.value();
  d->next_iova = kIovaBase;
  return d;
}

void domain_destroy(Domain* d) {
  {
    sync::SpinGuard guard(g_lock);
    for (uint32_t i = 0; i < function_count(); ++i) {
      if (g_owner[i] == d) detach_locked(i);
    }
    invalidate_domain(d);
    free_tree(d->root_phys, g_levels);
    free_id(d->id);
  }
  mm::kfree(d);
}

bool attach(Domain* d, uint32_t pci_index) {
  sync::SpinGuard guard(g_lock);
  if (g_unit_of[pci_index] < 0) return false;
  detach_locked(pci_index);
  Unit& u = g_units[g_unit_of[pci_index]];
  volatile Entry128* e = context_entry(u, function(pci_index), true);
  if (e == nullptr) return false;
  store(&e->hi, g_aw | uint64_t{d->id} << 8);
  store(&e->lo, d->root_phys | kPresent);  // TT = 00, P — последним
  invalidate_context(u);
  invalidate_iotlb(u, kIotlbDomain | iotlb_did(d->id));
  g_owner[pci_index] = d;
  return true;
}

uint64_t map(Domain* d, const uint64_t* frames, uint64_t pages, bool writable) {
  sync::SpinGuard guard(g_lock);
  uint64_t iova = d->next_iova;
  if (pages == 0 || pages > (kIovaLimit - iova) / kPageSize) return 0;
  for (uint64_t i = 0; i < pages; ++i) {
    volatile uint64_t* e = leaf(d, iova + i * kPageSize, true);
    if (e == nullptr) {
      // Таблицы, уже построенные, остаются домену до его уничтожения.
      for (uint64_t k = 0; k < i; ++k) store(leaf(d, iova + k * kPageSize, false), 0);
      invalidate_domain(d);
      return 0;
    }
    store(e, frames[i] | kSsRead | (writable ? kSsWrite : 0));
  }
  d->next_iova = iova + pages * kPageSize;
  // Новые записи (не-присутствие → присутствие) при CM = 0 инвалидации не
  // требуют; при CM = 1 (эмуляция) — требуют (§6.1). Делаем всегда.
  invalidate_domain(d);
  return iova;
}

void unmap(Domain* d, uint64_t iova, uint64_t pages) {
  sync::SpinGuard guard(g_lock);
  for (uint64_t i = 0; i < pages; ++i) {
    volatile uint64_t* e = leaf(d, iova + i * kPageSize, false);
    if (e != nullptr) store(e, 0);
  }
  invalidate_domain(d);
}

void set_fault_handler(FaultHandler handler) { g_fault_handler = handler; }

uint64_t fault_count() {
  sync::SpinGuard guard(g_lock);
  return g_faults;
}

}  // namespace kernel::pci::iommu
