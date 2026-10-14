#include "memmap.hpp"

namespace boot {

using abi::boot::MemoryRegion;
using AbiType = abi::boot::MemoryType;

AbiType convert_memory_type(uint32_t efi_type) {
  switch (static_cast<efi::MemoryType>(efi_type)) {
    case efi::MemoryType::Conventional:
    case efi::MemoryType::BootServicesCode:
    case efi::MemoryType::BootServicesData:
      return AbiType::Usable;
    case efi::MemoryType::LoaderCode:
    case efi::MemoryType::LoaderData:
    case efi::MemoryType::OsBootloaderReclaimable:
      return AbiType::BootloaderReclaimable;
    case efi::MemoryType::OsKernelImage:
      return AbiType::KernelImage;
    case efi::MemoryType::OsInitrd:
      return AbiType::Initrd;
    case efi::MemoryType::AcpiReclaim:
      return AbiType::AcpiReclaimable;
    case efi::MemoryType::AcpiNvs:
      return AbiType::AcpiNvs;
    case efi::MemoryType::Unusable:
      return AbiType::BadMemory;
    default:
      // Runtime Services, MMIO, Reserved, PalCode, Persistent, Unaccepted
      // и неизвестные типы ядро не трогает.
      return AbiType::Reserved;
  }
}

size_t convert_memory_map(const uint8_t* efi_map, size_t efi_count, size_t descriptor_size,
                          uint64_t fb_base, uint64_t fb_size, MemoryRegion* out,
                          size_t out_cap) {
  size_t n = 0;
  bool fb_overlaps = false;
  for (size_t i = 0; i < efi_count; ++i) {
    const auto* d = reinterpret_cast<const efi::MemoryDescriptor*>(efi_map + i * descriptor_size);
    if (d->number_of_pages == 0) continue;
    if (n == out_cap) return 0;
    uint64_t len = d->number_of_pages * 4096;
    out[n++] = {d->physical_start, len, convert_memory_type(d->type), 0};
    if (fb_size != 0 && fb_base < d->physical_start + len && d->physical_start < fb_base + fb_size) {
      fb_overlaps = true;
    }
  }
  if (fb_size != 0 && !fb_overlaps) {
    if (n == out_cap) return 0;
    uint64_t base = fb_base & ~uint64_t{4095};
    uint64_t end = (fb_base + fb_size + 4095) & ~uint64_t{4095};
    out[n++] = {base, end - base, AbiType::Framebuffer, 0};
  }

  // Сортировка вставками: карта короткая и почти отсортирована.
  for (size_t i = 1; i < n; ++i) {
    MemoryRegion r = out[i];
    size_t j = i;
    while (j > 0 && out[j - 1].base > r.base) {
      out[j] = out[j - 1];
      --j;
    }
    out[j] = r;
  }

  size_t m = 0;
  for (size_t i = 0; i < n; ++i) {
    if (m > 0 && out[m - 1].type == out[i].type &&
        out[m - 1].base + out[m - 1].length == out[i].base) {
      out[m - 1].length += out[i].length;
    } else {
      out[m++] = out[i];
    }
  }
  return m;
}

}  // namespace boot
