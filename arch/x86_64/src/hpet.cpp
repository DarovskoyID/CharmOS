#include <arch/x86_64/cpu.hpp>
#include <arch/x86_64/hpet.hpp>
#include <arch/x86_64/mmio.hpp>
#include <lib/core/math.hpp>

namespace arch::x86_64 {

namespace {

// Регистры (HPET 1.0a §2.3.1).
constexpr uint64_t kCapabilities = 0x000;
constexpr uint64_t kConfig = 0x010;
constexpr uint64_t kMainCounter = 0x0f0;
constexpr uint64_t timer_config(uint32_t n) { return 0x100 + 0x20 * uint64_t{n}; }
constexpr uint64_t timer_comparator(uint32_t n) { return 0x108 + 0x20 * uint64_t{n}; }

// General Capabilities (§2.3.4).
constexpr uint64_t kCapCountSize64 = 1ull << 13;
// General Configuration (§2.3.5).
constexpr uint64_t kCfgEnable = 1ull << 0;
constexpr uint64_t kCfgLegacyRoute = 1ull << 1;
// Timer N Configuration (§2.3.8).
constexpr uint64_t kTnIntTypeLevel = 1ull << 1;
constexpr uint64_t kTnIntEnable = 1ull << 2;
constexpr uint64_t kTnPeriodic = 1ull << 3;
constexpr uint64_t kTnFsbEnable = 1ull << 14;
constexpr uint64_t kTnRouteShift = 9;
constexpr uint64_t kTnRouteMask = 0x1full << kTnRouteShift;

constexpr uint64_t kFsPerNs = 1'000'000;
// Спецификация: период не больше 100 нс (частота не ниже 10 МГц).
constexpr uint64_t kMaxPeriodFs = 100 * kFsPerNs;

}  // namespace

bool Hpet::init(uint64_t mmio_base) {
  base_ = mmio_base;
  uint64_t caps = mmio_read64(base_ + kCapabilities);
  period_fs_ = caps >> 32;
  if (period_fs_ == 0 || period_fs_ > kMaxPeriodFs) return false;
  wide_ = (caps & kCapCountSize64) != 0;
  timers_ = static_cast<uint32_t>((caps >> 8) & 0x1f) + 1;

  uint64_t cfg = mmio_read64(base_ + kConfig);
  cfg &= ~(kCfgEnable | kCfgLegacyRoute);
  mmio_write64(base_ + kConfig, cfg);
  for (uint32_t n = 0; n < timers_; ++n) disarm(n);
  mmio_write64(base_ + kMainCounter, 0);  // писать можно только при остановленном счётчике
  mmio_write64(base_ + kConfig, cfg | kCfgEnable);
  return true;
}

uint64_t Hpet::counter() const {
  uint64_t v = mmio_read64(base_ + kMainCounter);
  return wide_ ? v : (v & 0xffffffffull);
}

uint64_t Hpet::elapsed(uint64_t from, uint64_t to) const {
  return wide_ ? to - from : ((to - from) & 0xffffffffull);
}

uint64_t Hpet::ns_to_ticks(uint64_t ns) const {
  return lib::core::mul_div(ns, kFsPerNs, period_fs_);
}

uint64_t Hpet::ticks_to_ns(uint64_t ticks) const {
  return lib::core::mul_div(ticks, period_fs_, kFsPerNs);
}

void Hpet::wait_ns(uint64_t ns) const {
  uint64_t start = counter();
  uint64_t ticks = ns_to_ticks(ns);
  while (elapsed(start, counter()) < ticks) pause();
}

uint32_t Hpet::route_capability(uint32_t n) const {
  return static_cast<uint32_t>(mmio_read64(base_ + timer_config(n)) >> 32);
}

void Hpet::arm_one_shot(uint32_t n, uint32_t gsi, uint64_t ticks) const {
  uint64_t cfg = mmio_read64(base_ + timer_config(n));
  cfg &= ~(kTnIntTypeLevel | kTnPeriodic | kTnFsbEnable | kTnRouteMask | kTnIntEnable);
  cfg |= (uint64_t{gsi} << kTnRouteShift) & kTnRouteMask;
  mmio_write64(base_ + timer_config(n), cfg);
  mmio_write64(base_ + timer_comparator(n), counter() + ticks);
  mmio_write64(base_ + timer_config(n), cfg | kTnIntEnable);
}

void Hpet::disarm(uint32_t n) const {
  uint64_t cfg = mmio_read64(base_ + timer_config(n));
  mmio_write64(base_ + timer_config(n), cfg & ~kTnIntEnable);
}

}  // namespace arch::x86_64
