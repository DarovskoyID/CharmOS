#pragma once
// Boot ABI v1.0: передача управления загрузчик → ядро.
// Нормативный документ — docs/abi/boot.md. Любое изменение — сначала там.

#include <stddef.h>
#include <stdint.h>

namespace abi::boot {

inline constexpr uint64_t kMagic = 0x544f4f42534d5243;  // "CRMSBOOT" little-endian
inline constexpr uint16_t kVersionMajor = 1;
inline constexpr uint16_t kVersionMinor = 0;
inline constexpr uint64_t kKernelBase = 0xffffffff80000000;
inline constexpr uint64_t kHhdmBase = 0xffff800000000000;

inline constexpr uint64_t kFlagIdentityMapLow4G = 1ull << 0;

enum class MemoryType : uint32_t {
  Usable = 1,
  BootloaderReclaimable = 2,
  KernelImage = 3,
  Framebuffer = 4,
  AcpiReclaimable = 5,
  AcpiNvs = 6,
  Reserved = 7,
  BadMemory = 8,
  Initrd = 9,
};

struct MemoryRegion {
  uint64_t base;
  uint64_t length;
  MemoryType type;
  uint32_t reserved;
};

enum class PixelFormat : uint32_t {
  Rgbx8888 = 1,
  Bgrx8888 = 2,
};

struct Framebuffer {
  uint64_t base;
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
  PixelFormat format;
  uint64_t reserved;
};

struct BootInfo {
  uint64_t magic;
  uint16_t version_major;
  uint16_t version_minor;
  uint32_t size;
  uint64_t flags;

  uint64_t memory_map;
  uint64_t memory_map_count;

  uint64_t kernel_phys_base;
  uint64_t kernel_phys_size;
  uint64_t hhdm_base;

  Framebuffer framebuffer;

  uint64_t acpi_rsdp;
  uint64_t efi_system_table;

  uint64_t cmdline;
  uint64_t cmdline_length;

  uint64_t initrd_base;
  uint64_t initrd_size;

  uint64_t boot_time_tsc;
  uint64_t reserved[8];
};

static_assert(sizeof(MemoryRegion) == 24);
static_assert(offsetof(MemoryRegion, type) == 16);

static_assert(sizeof(Framebuffer) == 32);
static_assert(offsetof(Framebuffer, pitch) == 16);
static_assert(offsetof(Framebuffer, format) == 20);

static_assert(offsetof(BootInfo, version_major) == 8);
static_assert(offsetof(BootInfo, size) == 12);
static_assert(offsetof(BootInfo, flags) == 16);
static_assert(offsetof(BootInfo, memory_map) == 24);
static_assert(offsetof(BootInfo, kernel_phys_base) == 40);
static_assert(offsetof(BootInfo, hhdm_base) == 56);
static_assert(offsetof(BootInfo, framebuffer) == 64);
static_assert(offsetof(BootInfo, acpi_rsdp) == 96);
static_assert(offsetof(BootInfo, efi_system_table) == 104);
static_assert(offsetof(BootInfo, cmdline) == 112);
static_assert(offsetof(BootInfo, initrd_base) == 128);
static_assert(offsetof(BootInfo, boot_time_tsc) == 144);
static_assert(offsetof(BootInfo, reserved) == 152);
static_assert(sizeof(BootInfo) == 216);

}  // namespace abi::boot
