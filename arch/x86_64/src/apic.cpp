#include <arch/x86_64/apic.hpp>
#include <arch/x86_64/cpu.hpp>
#include <arch/x86_64/io.hpp>
#include <arch/x86_64/mmio.hpp>

namespace arch::x86_64 {

namespace {

// Регистры Local APIC (SDM табл. 11-1), смещения от базы.
constexpr uint32_t kRegId = 0x20;
constexpr uint32_t kRegVersion = 0x30;
constexpr uint32_t kRegTpr = 0x80;
constexpr uint32_t kRegEoi = 0xb0;
constexpr uint32_t kRegSvr = 0xf0;
constexpr uint32_t kRegLvtTimer = 0x320;
constexpr uint32_t kRegLvtLint0 = 0x350;
constexpr uint32_t kRegLvtLint1 = 0x360;
constexpr uint32_t kRegLvtError = 0x370;
constexpr uint32_t kRegTimerInitial = 0x380;
constexpr uint32_t kRegTimerCurrent = 0x390;
constexpr uint32_t kRegTimerDivide = 0x3e0;
constexpr uint32_t kRegEsr = 0x280;
constexpr uint32_t kRegIcrLow = 0x300;
constexpr uint32_t kRegIcrHigh = 0x310;

// x2APIC: регистр со смещением R доступен как MSR 0x800 + R/16 (SDM табл. 11-6).
constexpr uint32_t kX2ApicMsrBase = 0x800;

// ICR (SDM рис. 11-12).
constexpr uint32_t kIcrDeliveryFixed = 0x0u << 8;
constexpr uint32_t kIcrDeliveryInit = 0x5u << 8;
constexpr uint32_t kIcrDeliveryStartup = 0x6u << 8;
constexpr uint32_t kIcrDeliveryPending = 1u << 12;
constexpr uint32_t kIcrLevelAssert = 1u << 14;
constexpr uint32_t kIcrShorthandAllButSelf = 0x3u << 18;

constexpr uint32_t kSvrEnable = 1u << 8;
constexpr uint32_t kLvtMasked = 1u << 16;
constexpr uint32_t kLvtTimerPeriodic = 1u << 17;  // режим, биты 18:17 = 01
constexpr uint32_t kLvtDeliveryNmi = 0x4u << 8;   // SDM рис. 11-8: 100 = NMI

// I/O APIC (даташит 82093AA §3.1): IOREGSEL по смещению 0, IOWIN — 0x10.
constexpr uint64_t kIoRegSel = 0x00;
constexpr uint64_t kIoWin = 0x10;
constexpr uint8_t kIoApicVer = 0x01;
constexpr uint8_t kIoRedTbl = 0x10;
constexpr uint32_t kRedMasked = 1u << 16;
constexpr uint32_t kRedLevel = 1u << 15;
constexpr uint32_t kRedActiveLow = 1u << 13;

// 8259: регистр маски (OCW1) — адрес с A0=1.
constexpr uint16_t kPicMasterData = 0x21;
constexpr uint16_t kPicSlaveData = 0xa1;

}  // namespace

void mask_legacy_pic() {
  outb(kPicMasterData, 0xff);
  outb(kPicSlaveData, 0xff);
}

// --- Local APIC ------------------------------------------------------------------

bool cpu_has_x2apic() {
  uint32_t eax, ebx, ecx, edx;
  asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1u), "c"(0u));
  return (ecx & (1u << 21)) != 0;
}

uint32_t LocalApic::read(uint32_t reg) const {
  if (mode_ == Mode::X2Apic) return static_cast<uint32_t>(rdmsr(kX2ApicMsrBase + (reg >> 4)));
  return mmio_read32(base_ + reg);
}

void LocalApic::write(uint32_t reg, uint32_t v) const {
  if (mode_ == Mode::X2Apic) {
    wrmsr(kX2ApicMsrBase + (reg >> 4), v);
  } else {
    mmio_write32(base_ + reg, v);
  }
}

void LocalApic::configure(Mode mode, uint64_t mmio_base) {
  mode_ = mode;
  base_ = mmio_base;
}

void LocalApic::init_this_cpu(uint8_t spurious_vector, uint8_t error_vector) const {
  uint64_t msr = rdmsr(kMsrApicBase) | kApicBaseEnable;
  // Табл. 11-5: x2APIC включается поверх включённого xAPIC; обратный переход
  // (x2APIC → xAPIC) без сброса не разрешён, поэтому режим только повышаем.
  if (mode_ == Mode::X2Apic) msr |= kApicBaseX2Apic;
  wrmsr(kMsrApicBase, msr);

  write(kRegTpr, 0);  // принимать прерывания всех приоритетов
  write(kRegLvtTimer, kLvtMasked);
  write(kRegLvtLint0, kLvtMasked);
  write(kRegLvtLint1, kLvtMasked);
  write(kRegLvtError, error_vector);
  // ESR обновляется записью перед чтением (SDM §11.5.3); в x2APIC писать можно только 0.
  write(kRegEsr, 0);
  write(kRegEsr, 0);
  write(kRegSvr, kSvrEnable | spurious_vector);
  eoi();  // на случай необслуженного прерывания от прошивки
}

uint32_t LocalApic::id() const {
  // xAPIC: ID в битах 31:24; x2APIC: весь регистр (SDM §11.12.5.1).
  return mode_ == Mode::X2Apic ? read(kRegId) : read(kRegId) >> 24;
}

uint32_t LocalApic::version() const { return read(kRegVersion); }

void LocalApic::eoi() const { write(kRegEoi, 0); }

void LocalApic::send_ipi(uint32_t apic_id, uint32_t low) const {
  if (mode_ == Mode::X2Apic) {
    // SDM §11.12.9: ICR — один 64-битный MSR, назначение в битах 63:32.
    wrmsr(kX2ApicMsrBase + (kRegIcrLow >> 4), (uint64_t{apic_id} << 32) | low);
    return;
  }
  // xAPIC: сначала старшая половина (назначение в битах 31:24), запись младшей
  // отправляет IPI. Ждём, пока предыдущее не уйдёт (Delivery Status).
  while ((read(kRegIcrLow) & kIcrDeliveryPending) != 0) pause();
  write(kRegIcrHigh, apic_id << 24);
  write(kRegIcrLow, low);
  while ((read(kRegIcrLow) & kIcrDeliveryPending) != 0) pause();
}

void LocalApic::send_init(uint32_t apic_id) const {
  send_ipi(apic_id, kIcrDeliveryInit | kIcrLevelAssert);
}

void LocalApic::send_startup(uint32_t apic_id, uint8_t vector_page) const {
  send_ipi(apic_id, kIcrDeliveryStartup | kIcrLevelAssert | vector_page);
}

void LocalApic::send_fixed(uint32_t apic_id, uint8_t vector) const {
  send_ipi(apic_id, kIcrDeliveryFixed | kIcrLevelAssert | vector);
}

void LocalApic::send_fixed_all_but_self(uint8_t vector) const {
  send_ipi(0, kIcrShorthandAllButSelf | kIcrDeliveryFixed | kIcrLevelAssert | vector);
}

void LocalApic::timer_one_shot(uint8_t vector, uint32_t divide, uint32_t initial_count) const {
  write(kRegTimerDivide, divide);
  write(kRegLvtTimer, vector);
  write(kRegTimerInitial, initial_count);
}

void LocalApic::timer_periodic(uint8_t vector, uint32_t divide, uint32_t initial_count) const {
  write(kRegTimerDivide, divide);
  write(kRegLvtTimer, kLvtTimerPeriodic | vector);
  write(kRegTimerInitial, initial_count);
}

void LocalApic::timer_stop() const {
  write(kRegLvtTimer, kLvtMasked);
  write(kRegTimerInitial, 0);
}

uint32_t LocalApic::timer_current() const { return read(kRegTimerCurrent); }

void LocalApic::mask_lint(int lint) const {
  write(lint == 0 ? kRegLvtLint0 : kRegLvtLint1, kLvtMasked);
}

void LocalApic::set_lint_nmi(int lint) const {
  write(lint == 0 ? kRegLvtLint0 : kRegLvtLint1, kLvtDeliveryNmi);
}

// --- I/O APIC --------------------------------------------------------------------

uint32_t IoApic::read(uint8_t reg) const {
  mmio_write32(base_ + kIoRegSel, reg);
  return mmio_read32(base_ + kIoWin);
}

void IoApic::write(uint8_t reg, uint32_t v) const {
  mmio_write32(base_ + kIoRegSel, reg);
  mmio_write32(base_ + kIoWin, v);
}

void IoApic::init(uint64_t mmio_base, uint32_t gsi_base) {
  base_ = mmio_base;
  gsi_base_ = gsi_base;
  count_ = ((read(kIoApicVer) >> 16) & 0xff) + 1;  // Maximum Redirection Entry + 1
  for (uint32_t i = 0; i < count_; ++i) mask(gsi_base_ + i);
}

uint32_t IoApic::version() const { return read(kIoApicVer) & 0xff; }

void IoApic::route(uint32_t gsi, const Route& r) const {
  auto reg = static_cast<uint8_t>(kIoRedTbl + 2 * (gsi - gsi_base_));
  uint32_t low = r.vector;  // режим доставки 000 (fixed), физическое назначение
  if (r.level_triggered) low |= kRedLevel;
  if (r.active_low) low |= kRedActiveLow;
  write(reg, kRedMasked);  // сначала маска: запись в две половины не атомарна
  write(static_cast<uint8_t>(reg + 1), uint32_t{r.dest_apic_id} << 24);
  write(reg, low);
}

void IoApic::mask(uint32_t gsi) const {
  auto reg = static_cast<uint8_t>(kIoRedTbl + 2 * (gsi - gsi_base_));
  write(reg, kRedMasked);
  write(static_cast<uint8_t>(reg + 1), 0);
}

}  // namespace arch::x86_64
