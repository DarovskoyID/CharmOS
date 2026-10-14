#pragma once
// Доступ к регистрам устройств. volatile гарантирует, что компилятор не
// объединит и не переставит обращения; отображение некэшируемое (vmm::map_mmio),
// поэтому процессор тоже не переупорядочивает их относительно друг друга.

#include <stdint.h>

namespace arch::x86_64 {

inline uint32_t mmio_read32(uint64_t addr) {
  return *reinterpret_cast<volatile const uint32_t*>(addr);
}
inline void mmio_write32(uint64_t addr, uint32_t v) {
  *reinterpret_cast<volatile uint32_t*>(addr) = v;
}
inline uint64_t mmio_read64(uint64_t addr) {
  return *reinterpret_cast<volatile const uint64_t*>(addr);
}
inline void mmio_write64(uint64_t addr, uint64_t v) {
  *reinterpret_cast<volatile uint64_t*>(addr) = v;
}

}  // namespace arch::x86_64
