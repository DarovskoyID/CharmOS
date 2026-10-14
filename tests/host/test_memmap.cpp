#include "memmap.hpp"

#include "test.hpp"

using abi::boot::MemoryRegion;
using AbiType = abi::boot::MemoryType;

namespace {

// Прошивка вправе использовать дескриптор длиннее sizeof (шаг — descriptor_size).
constexpr size_t kStride = 48;

struct Map {
  alignas(8) uint8_t buf[16 * kStride] = {};
  size_t count = 0;
  void add(efi::MemoryType t, uint64_t base, uint64_t pages) {
    auto* d = reinterpret_cast<efi::MemoryDescriptor*>(buf + count++ * kStride);
    d->type = static_cast<uint32_t>(t);
    d->physical_start = base;
    d->number_of_pages = pages;
  }
};

}  // namespace

TEST(memmap_types) {
  using efi::MemoryType;
  CHECK(boot::convert_memory_type(uint32_t(MemoryType::Conventional)) == AbiType::Usable);
  CHECK(boot::convert_memory_type(uint32_t(MemoryType::BootServicesData)) == AbiType::Usable);
  CHECK(boot::convert_memory_type(uint32_t(MemoryType::LoaderData)) ==
        AbiType::BootloaderReclaimable);
  CHECK(boot::convert_memory_type(uint32_t(MemoryType::OsKernelImage)) == AbiType::KernelImage);
  CHECK(boot::convert_memory_type(uint32_t(MemoryType::RuntimeServicesCode)) ==
        AbiType::Reserved);
  CHECK(boot::convert_memory_type(uint32_t(MemoryType::AcpiReclaim)) ==
        AbiType::AcpiReclaimable);
  CHECK(boot::convert_memory_type(uint32_t(MemoryType::Unusable)) == AbiType::BadMemory);
  CHECK(boot::convert_memory_type(0x12345) == AbiType::Reserved);
}

TEST(memmap_sort_merge_and_skip_empty) {
  Map m;
  m.add(efi::MemoryType::BootServicesData, 0x200000, 16);
  m.add(efi::MemoryType::Conventional, 0x100000, 256);  // смежен с предыдущим, тот же тип
  m.add(efi::MemoryType::OsKernelImage, 0x300000, 4);
  m.add(efi::MemoryType::Conventional, 0x900000, 0);    // пустой — выбрасывается
  m.add(efi::MemoryType::Reserved, 0, 1);
  MemoryRegion out[16];
  size_t n = boot::convert_memory_map(m.buf, m.count, kStride, 0, 0, out, 16);
  CHECK(n == 3);
  CHECK(out[0].base == 0 && out[0].type == AbiType::Reserved);
  CHECK(out[1].base == 0x100000 && out[1].length == 0x110000 && out[1].type == AbiType::Usable);
  CHECK(out[2].base == 0x300000 && out[2].type == AbiType::KernelImage);
}

TEST(memmap_framebuffer_added_when_absent) {
  Map m;
  m.add(efi::MemoryType::Conventional, 0x100000, 16);
  MemoryRegion out[16];
  size_t n = boot::convert_memory_map(m.buf, m.count, kStride, 0x80000010, 0x1000, out, 16);
  CHECK(n == 2);
  CHECK(out[1].base == 0x80000000 && out[1].length == 0x2000 &&
        out[1].type == AbiType::Framebuffer);
}

TEST(memmap_framebuffer_not_added_when_in_map) {
  Map m;
  m.add(efi::MemoryType::MemoryMappedIo, 0x80000000, 256);
  MemoryRegion out[16];
  size_t n = boot::convert_memory_map(m.buf, m.count, kStride, 0x80000000, 0x1000, out, 16);
  CHECK(n == 1 && out[0].type == AbiType::Reserved);
}

TEST(memmap_out_of_capacity) {
  Map m;
  m.add(efi::MemoryType::Conventional, 0x100000, 16);
  m.add(efi::MemoryType::Reserved, 0x200000, 16);
  MemoryRegion out[1];
  CHECK(boot::convert_memory_map(m.buf, m.count, kStride, 0, 0, out, 1) == 0);
}
