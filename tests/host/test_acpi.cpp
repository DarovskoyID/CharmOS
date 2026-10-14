#include <lib/acpi/parse.hpp>

#include "test.hpp"

using namespace lib::acpi;
using lib::core::StringView;

namespace {

// На хосте «физический адрес» таблицы — её адрес в памяти процесса.
const void* map_identity(void*, uint64_t phys, size_t) {
  return reinterpret_cast<const void*>(phys);
}

void fix_checksum(void* data, size_t length, size_t checksum_offset) {
  auto* p = static_cast<uint8_t*>(data);
  p[checksum_offset] = 0;
  uint8_t sum = 0;
  for (size_t i = 0; i < length; ++i) sum = static_cast<uint8_t>(sum + p[i]);
  p[checksum_offset] = static_cast<uint8_t>(0 - sum);
}

void init_header(void* buf, const char* sig, uint32_t length) {
  auto* h = static_cast<SdtHeader*>(buf);
  std::memcpy(h->signature, sig, 4);
  h->length = length;
  h->revision = 1;
  fix_checksum(buf, length, offsetof(SdtHeader, checksum));
}

struct Builder {
  alignas(8) uint8_t buf[512] = {};
  size_t len = 0;
  template <typename T>
  void add(const T& v) {
    std::memcpy(buf + len, &v, sizeof v);
    len += sizeof v;
  }
};

// MADT как у QEMU q35: 2 CPU (второй выключен), 1 IOAPIC, 2 переопределения, NMI.
Builder make_madt() {
  Builder b;
  MadtHeader mh = {};
  mh.local_apic_address = 0xfee00000;
  mh.flags = kMadtPcatCompat;
  b.add(mh);
  b.add(MadtLocalApic{{0, 8}, 0, 0, kLocalApicEnabled});
  b.add(MadtLocalApic{{0, 8}, 1, 1, kLocalApicOnlineCapable});
  b.add(MadtIoApic{{1, 12}, 0, 0, 0xfec00000, 0});
  b.add(MadtInterruptOverride{{2, 10}, 0, 0, 2, 0});
  b.add(MadtInterruptOverride{{2, 10}, 0, 9, 9, kIntiPolarityHigh | kIntiTriggerLevel});
  b.add(MadtLocalApicNmi{{4, 6}, 0xff, 0, 1});
  b.add(uint8_t{0x7f});  // неизвестный тип, длина 2 — должен пропускаться
  b.add(uint8_t{2});
  init_header(b.buf, "APIC", static_cast<uint32_t>(b.len));
  return b;
}

}  // namespace

TEST(acpi_checksum) {
  uint8_t data[4] = {1, 2, 3, 0};
  CHECK(!checksum_ok(data, 4));
  fix_checksum(data, 4, 3);
  CHECK(checksum_ok(data, 4));
}

TEST(acpi_rsdp) {
  Rsdp r = {};
  std::memcpy(r.signature, "RSD PTR ", 8);
  r.revision = 2;
  r.length = sizeof(Rsdp);
  r.xsdt_address = 0x1234000;
  fix_checksum(&r, 20, offsetof(Rsdp, checksum));
  fix_checksum(&r, sizeof r, offsetof(Rsdp, extended_checksum));
  CHECK(parse_rsdp(&r).value() == 0x1234000);

  Rsdp bad = r;
  bad.signature[0] = 'X';
  CHECK(parse_rsdp(&bad).error() == AcpiError::BadSignature);
  bad = r;
  bad.oem_id[0] = 'Z';  // ломает обе суммы
  CHECK(parse_rsdp(&bad).error() == AcpiError::BadChecksum);
  bad = r;
  bad.xsdt_address ^= 1;  // ломает только расширенную сумму
  CHECK(parse_rsdp(&bad).error() == AcpiError::BadChecksum);
  bad = r;
  bad.revision = 0;
  fix_checksum(&bad, 20, offsetof(Rsdp, checksum));
  CHECK(parse_rsdp(&bad).error() == AcpiError::NoXsdt);
}

TEST(acpi_validate_table) {
  alignas(8) uint8_t t[64] = {};
  init_header(t, "HPET", 64);
  CHECK(validate_table(t, 64, "HPET").is_ok());
  CHECK(validate_table(t, 64, "").is_ok());
  CHECK(validate_table(t, 64, "APIC").error() == AcpiError::BadSignature);
  CHECK(validate_table(t, 40, "HPET").error() == AcpiError::BadLength);
  t[50] ^= 0xff;
  CHECK(validate_table(t, 64, "HPET").error() == AcpiError::BadChecksum);
}

TEST(acpi_find_table_in_xsdt) {
  Builder madt = make_madt();
  alignas(8) uint8_t facp[40] = {};
  init_header(facp, "FACP", 40);
  alignas(8) uint8_t apic2[60] = {};
  std::memcpy(apic2, madt.buf, sizeof apic2);  // второй экземпляр той же сигнатуры
  reinterpret_cast<SdtHeader*>(apic2)->length = sizeof apic2;
  fix_checksum(apic2, sizeof apic2, offsetof(SdtHeader, checksum));

  alignas(8) uint8_t xsdt[36 + 3 * 8] = {};
  uint64_t ptrs[3] = {reinterpret_cast<uint64_t>(facp), reinterpret_cast<uint64_t>(madt.buf),
                      reinterpret_cast<uint64_t>(apic2)};
  std::memcpy(xsdt + 36, ptrs, sizeof ptrs);
  init_header(xsdt, "XSDT", sizeof xsdt);
  const auto* x = reinterpret_cast<const SdtHeader*>(xsdt);

  auto r = find_table(x, "APIC", 0, &map_identity, nullptr);
  CHECK(r.is_ok() && r.value() == reinterpret_cast<const SdtHeader*>(madt.buf));
  auto r2 = find_table(x, "APIC", 1, &map_identity, nullptr);
  CHECK(r2.is_ok() && r2.value() == reinterpret_cast<const SdtHeader*>(apic2));
  CHECK(find_table(x, "APIC", 2, &map_identity, nullptr).error() == AcpiError::NotFound);
  CHECK(find_table(x, "HPET", 0, &map_identity, nullptr).error() == AcpiError::NotFound);
  facp[20] ^= 1;  // порча найденной таблицы обнаруживается
  CHECK(find_table(x, "FACP", 0, &map_identity, nullptr).error() == AcpiError::BadChecksum);
}

TEST(acpi_parse_madt) {
  Builder b = make_madt();
  static MadtInfo info;
  CHECK(parse_madt(reinterpret_cast<const SdtHeader*>(b.buf), &info).is_ok());
  CHECK(info.local_apic_address == 0xfee00000);
  CHECK(info.pcat_compat);
  CHECK(info.cpu_count == 2);
  CHECK(info.cpus[0].apic_id == 0 && info.cpus[0].enabled);
  CHECK(info.cpus[1].apic_id == 1 && !info.cpus[1].enabled && info.cpus[1].online_capable);
  CHECK(info.io_apic_count == 1 && info.io_apics[0].address == 0xfec00000);
  CHECK(info.override_count == 2);
  CHECK(info.nmi_lint == 1);

  uint16_t flags = 0xffff;
  CHECK(isa_irq_to_gsi(info, 0, &flags) == 2 && flags == 0);
  CHECK(isa_irq_to_gsi(info, 9, &flags) == 9 && flags == (kIntiPolarityHigh | kIntiTriggerLevel));
  CHECK(isa_irq_to_gsi(info, 4, &flags) == 4 && flags == 0);
}

TEST(acpi_parse_madt_rejects_bad_entry_length) {
  Builder b = make_madt();
  b.buf[sizeof(MadtHeader) + 1] = 0;  // длина первой записи = 0
  static MadtInfo info;
  CHECK(parse_madt(reinterpret_cast<const SdtHeader*>(b.buf), &info).error() ==
        AcpiError::BadLength);
}

TEST(acpi_parse_hpet) {
  HpetTable t = {};
  t.base_address = {0, 64, 0, 0, 0xfed00000};
  t.hpet_number = 0;
  t.minimum_tick = 128;
  init_header(&t, "HPET", sizeof t);
  HpetInfo info;
  CHECK(parse_hpet(&t.header, &info).is_ok());
  CHECK(info.base_address == 0xfed00000 && info.minimum_tick == 128);
  t.base_address.address_space = 1;  // порты ввода-вывода не поддерживаем
  CHECK(parse_hpet(&t.header, &info).error() == AcpiError::Unsupported);
}

TEST(acpi_parse_fadt) {
  alignas(8) uint8_t t[116] = {};
  t[46] = 9;
  t[109] = 0x2;  // 8042 есть
  t[112] = 0x10;
  init_header(t, "FACP", sizeof t);
  FadtInfo f;
  CHECK(parse_fadt(reinterpret_cast<const SdtHeader*>(t), &f).is_ok());
  CHECK(f.sci_interrupt == 9 && f.iapc_boot_arch == 2 && f.flags == 0x10);
  // Короткая таблица ACPI 1.0: только SCI.
  alignas(8) uint8_t old[100] = {};
  old[46] = 5;
  init_header(old, "FACP", sizeof old);
  CHECK(parse_fadt(reinterpret_cast<const SdtHeader*>(old), &f).is_ok());
  CHECK(f.sci_interrupt == 5 && f.iapc_boot_arch == 0 && f.flags == 0);
}

TEST(acpi_parse_mcfg) {
  alignas(8) uint8_t t[36 + 8 + 2 * 16] = {};
  uint64_t base0 = 0xb0000000, base1 = 0x380000000000ull;
  std::memcpy(t + 44, &base0, 8);
  t[44 + 10] = 0;
  t[44 + 11] = 0xff;
  std::memcpy(t + 60, &base1, 8);
  t[60 + 8] = 1;  // сегмент 1
  t[60 + 10] = 0x10;
  t[60 + 11] = 0x1f;
  init_header(t, "MCFG", sizeof t);
  McfgInfo info;
  CHECK(parse_mcfg(reinterpret_cast<const SdtHeader*>(t), &info).is_ok());
  CHECK(info.count == 2);
  CHECK(info.windows[0].base == base0 && info.windows[0].end_bus == 0xff);
  CHECK(info.windows[1].segment == 1 && info.windows[1].start_bus == 0x10);
  reinterpret_cast<SdtHeader*>(t)->length = 36 + 8 + 20;  // не кратно записи
  CHECK(parse_mcfg(reinterpret_cast<const SdtHeader*>(t), &info).error() == AcpiError::BadLength);
}
