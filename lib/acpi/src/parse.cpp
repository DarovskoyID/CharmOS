#include <lib/acpi/parse.hpp>

namespace lib::acpi {

using lib::core::Result;
using lib::core::Status;
using lib::core::StringView;

namespace {

Status<AcpiError> fail(AcpiError e) { return Status<AcpiError>::err(e); }
Status<AcpiError> ok() { return lib::core::ok_status<AcpiError>(); }

template <typename T>
T read(const uint8_t* p) {
  T v;
  __builtin_memcpy(&v, p, sizeof v);  // таблицы ACPI не выровнены
  return v;
}

}  // namespace

const char* error_name(AcpiError e) {
  switch (e) {
    case AcpiError::BadSignature: return "bad signature";
    case AcpiError::BadChecksum: return "bad checksum";
    case AcpiError::BadLength: return "bad length";
    case AcpiError::NoXsdt: return "no XSDT (ACPI 1.0 RSDP)";
    case AcpiError::NotFound: return "table not found";
    case AcpiError::MapFailed: return "cannot map table";
    case AcpiError::TooMany: return "too many MADT entries";
    case AcpiError::Unsupported: return "unsupported";
  }
  return "unknown";
}

bool checksum_ok(const void* data, size_t length) {
  const auto* p = static_cast<const uint8_t*>(data);
  uint8_t sum = 0;
  for (size_t i = 0; i < length; ++i) sum = static_cast<uint8_t>(sum + p[i]);
  return sum == 0;
}

Result<uint64_t, AcpiError> parse_rsdp(const void* data) {
  using R = Result<uint64_t, AcpiError>;
  const auto* r = static_cast<const Rsdp*>(data);
  if (StringView(r->signature, 8) != StringView("RSD PTR ")) return R::err(AcpiError::BadSignature);
  if (!checksum_ok(r, 20)) return R::err(AcpiError::BadChecksum);
  if (r->revision < 2) return R::err(AcpiError::NoXsdt);
  if (r->length < sizeof(Rsdp)) return R::err(AcpiError::BadLength);
  if (!checksum_ok(r, sizeof(Rsdp))) return R::err(AcpiError::BadChecksum);
  if (r->xsdt_address == 0) return R::err(AcpiError::NoXsdt);
  return R::ok(r->xsdt_address);
}

Status<AcpiError> validate_table(const void* table, size_t available, StringView signature) {
  const auto* h = static_cast<const SdtHeader*>(table);
  if (available < sizeof(SdtHeader)) return fail(AcpiError::BadLength);
  if (!signature.empty() && StringView(h->signature, 4) != signature) {
    return fail(AcpiError::BadSignature);
  }
  if (h->length < sizeof(SdtHeader) || h->length > available) return fail(AcpiError::BadLength);
  if (!checksum_ok(h, h->length)) return fail(AcpiError::BadChecksum);
  return ok();
}

Result<const SdtHeader*, AcpiError> find_table(const SdtHeader* xsdt, StringView signature,
                                               size_t index, MapFn map, void* ctx) {
  using R = Result<const SdtHeader*, AcpiError>;
  const auto* entries = reinterpret_cast<const uint8_t*>(xsdt) + sizeof(SdtHeader);
  size_t count = (xsdt->length - sizeof(SdtHeader)) / 8;
  for (size_t i = 0; i < count; ++i) {
    uint64_t phys = read<uint64_t>(entries + i * 8);
    const auto* h = static_cast<const SdtHeader*>(map(ctx, phys, sizeof(SdtHeader)));
    if (h == nullptr) return R::err(AcpiError::MapFailed);
    if (StringView(h->signature, 4) != signature) continue;
    if (index-- != 0) continue;
    uint32_t length = h->length;
    if (length < sizeof(SdtHeader)) return R::err(AcpiError::BadLength);
    const auto* full = static_cast<const SdtHeader*>(map(ctx, phys, length));
    if (full == nullptr) return R::err(AcpiError::MapFailed);
    auto v = validate_table(full, length, signature);
    if (v.is_err()) return R::err(v.error());
    return R::ok(full);
  }
  return R::err(AcpiError::NotFound);
}

Status<AcpiError> parse_madt(const SdtHeader* madt, MadtInfo* out) {
  if (madt->length < sizeof(MadtHeader)) return fail(AcpiError::BadLength);
  const auto* base = reinterpret_cast<const uint8_t*>(madt);
  out->local_apic_address = read<uint32_t>(base + offsetof(MadtHeader, local_apic_address));
  out->pcat_compat = (read<uint32_t>(base + offsetof(MadtHeader, flags)) & kMadtPcatCompat) != 0;
  out->cpu_count = 0;
  out->io_apic_count = 0;
  out->override_count = 0;
  out->nmi_lint = 0xff;
  out->nmi_flags = 0;

  size_t off = sizeof(MadtHeader);
  while (off + sizeof(MadtEntryHeader) <= madt->length) {
    const uint8_t* e = base + off;
    uint8_t type = e[0];
    uint8_t len = e[1];
    if (len < sizeof(MadtEntryHeader) || off + len > madt->length) {
      return fail(AcpiError::BadLength);
    }
    switch (static_cast<MadtType>(type)) {
      case MadtType::LocalApic: {
        if (len < sizeof(MadtLocalApic)) return fail(AcpiError::BadLength);
        uint32_t flags = read<uint32_t>(e + offsetof(MadtLocalApic, flags));
        if (out->cpu_count == MadtInfo::kMaxCpus) return fail(AcpiError::TooMany);
        out->cpus[out->cpu_count++] = {e[offsetof(MadtLocalApic, apic_id)],
                                       e[offsetof(MadtLocalApic, processor_uid)],
                                       (flags & kLocalApicEnabled) != 0,
                                       (flags & kLocalApicOnlineCapable) != 0};
        break;
      }
      case MadtType::LocalX2Apic: {
        if (len < sizeof(MadtLocalX2Apic)) return fail(AcpiError::BadLength);
        uint32_t flags = read<uint32_t>(e + offsetof(MadtLocalX2Apic, flags));
        if (out->cpu_count == MadtInfo::kMaxCpus) return fail(AcpiError::TooMany);
        out->cpus[out->cpu_count++] = {read<uint32_t>(e + offsetof(MadtLocalX2Apic, x2apic_id)),
                                       read<uint32_t>(e + offsetof(MadtLocalX2Apic, processor_uid)),
                                       (flags & kLocalApicEnabled) != 0,
                                       (flags & kLocalApicOnlineCapable) != 0};
        break;
      }
      case MadtType::IoApic: {
        if (len < sizeof(MadtIoApic)) return fail(AcpiError::BadLength);
        if (out->io_apic_count == MadtInfo::kMaxIoApics) return fail(AcpiError::TooMany);
        out->io_apics[out->io_apic_count++] = {e[offsetof(MadtIoApic, io_apic_id)],
                                               read<uint32_t>(e + offsetof(MadtIoApic, address)),
                                               read<uint32_t>(e + offsetof(MadtIoApic, gsi_base))};
        break;
      }
      case MadtType::InterruptSourceOverride: {
        if (len < sizeof(MadtInterruptOverride)) return fail(AcpiError::BadLength);
        if (e[offsetof(MadtInterruptOverride, bus)] != 0) break;  // только ISA
        if (out->override_count == MadtInfo::kMaxOverrides) return fail(AcpiError::TooMany);
        out->overrides[out->override_count++] = {
            e[offsetof(MadtInterruptOverride, source)],
            read<uint32_t>(e + offsetof(MadtInterruptOverride, gsi)),
            read<uint16_t>(e + offsetof(MadtInterruptOverride, flags))};
        break;
      }
      case MadtType::LocalApicNmi: {
        if (len < sizeof(MadtLocalApicNmi)) return fail(AcpiError::BadLength);
        out->nmi_lint = e[offsetof(MadtLocalApicNmi, lint)];
        out->nmi_flags = read<uint16_t>(e + offsetof(MadtLocalApicNmi, flags));
        break;
      }
      case MadtType::LocalApicAddressOverride: {
        if (len < sizeof(MadtLocalApicAddressOverride)) return fail(AcpiError::BadLength);
        out->local_apic_address =
            read<uint64_t>(e + offsetof(MadtLocalApicAddressOverride, address));
        break;
      }
      default:
        break;  // остальные типы M2 не нужны
    }
    off += len;
  }
  return ok();
}

uint32_t isa_irq_to_gsi(const MadtInfo& madt, uint8_t irq, uint16_t* flags) {
  for (size_t i = 0; i < madt.override_count; ++i) {
    if (madt.overrides[i].source == irq) {
      if (flags != nullptr) *flags = madt.overrides[i].flags;
      return madt.overrides[i].gsi;
    }
  }
  if (flags != nullptr) *flags = 0;
  return irq;
}

Status<AcpiError> parse_hpet(const SdtHeader* table, HpetInfo* out) {
  if (table->length < sizeof(HpetTable)) return fail(AcpiError::BadLength);
  const auto* base = reinterpret_cast<const uint8_t*>(table);
  GenericAddress gas = read<GenericAddress>(base + offsetof(HpetTable, base_address));
  if (gas.address_space != 0) return fail(AcpiError::Unsupported);  // только память
  out->base_address = gas.address;
  out->hpet_number = base[offsetof(HpetTable, hpet_number)];
  out->minimum_tick = read<uint16_t>(base + offsetof(HpetTable, minimum_tick));
  return ok();
}

}  // namespace lib::acpi

namespace lib::acpi {

lib::core::Status<AcpiError> parse_fadt(const SdtHeader* fadt, FadtInfo* out) {
  constexpr size_t kSciOffset = 46;
  constexpr size_t kBootArchOffset = 109;
  constexpr size_t kFlagsOffset = 112;
  if (fadt->length < kSciOffset + 2) return fail(AcpiError::BadLength);
  const auto* base = reinterpret_cast<const uint8_t*>(fadt);
  *out = {};
  out->revision = fadt->revision;
  out->sci_interrupt = read<uint16_t>(base + kSciOffset);
  if (fadt->length >= kBootArchOffset + 2) {
    out->iapc_boot_arch = read<uint16_t>(base + kBootArchOffset);
  }
  if (fadt->length >= kFlagsOffset + 4) out->flags = read<uint32_t>(base + kFlagsOffset);
  return ok();
}

}  // namespace lib::acpi
