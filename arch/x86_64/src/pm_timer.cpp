#include <arch/x86_64/pm_timer.hpp>
#include <lib/core/math.hpp>

namespace arch::x86_64 {

namespace {

uint32_t inl(uint16_t port) {
  uint32_t v;
  asm volatile("inl %1, %0" : "=a"(v) : "Nd"(port) : "memory");
  return v;
}

}  // namespace

void PmTimer::init(uint16_t port, uint64_t mmio, uint32_t bits) {
  port_ = port;
  mmio_ = mmio;
  mask_ = bits == 32 ? 0xffffffffull : 0xffffffull;
}

uint64_t PmTimer::counter() const {
  // §4.8.3.3: регистр читается 32-битным доступом.
  uint32_t v = mmio_ != 0 ? *reinterpret_cast<volatile const uint32_t*>(mmio_) : inl(port_);
  return v & mask_;
}

uint64_t PmTimer::ns_to_ticks(uint64_t ns) const {
  return lib::core::mul_div(ns, kHz, 1'000'000'000);
}

uint64_t PmTimer::ticks_to_ns(uint64_t ticks) const {
  return lib::core::mul_div(ticks, 1'000'000'000, kHz);
}

void PmTimer::wait_ns(uint64_t ns) const {
  // Переполнение 24-битного счётчика — раз в ~4.7 с: ждём кусками по 1 с.
  while (ns > 0) {
    uint64_t chunk = ns > 1'000'000'000 ? 1'000'000'000 : ns;
    uint64_t want = ns_to_ticks(chunk);
    uint64_t start = counter();
    while (elapsed(start, counter()) < want) asm volatile("pause");
    ns -= chunk;
  }
}

}  // namespace arch::x86_64
