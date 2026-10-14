// Загрузчик CharmOS (M1). Читает ядро с ESP, строит таблицы страниц,
// закрывает Boot Services и передаёт управление ядру по контракту
// docs/abi/boot.md. Структуры UEFI — docs/specs/uefi-2.11.pdf.

#include <abi/boot.hpp>
#include <arch/x86_64/serial.hpp>
#include <lib/core/format.hpp>
#include <lib/elf/elf64.hpp>

#include "efi.hpp"
#include "memmap.hpp"
#include <arch/x86_64/paging.hpp>

extern "C" [[noreturn]] void enter_kernel(uint64_t cr3, const void* gdtr, uint64_t stack_top,
                                          uint64_t entry, uint64_t boot_info_phys);

namespace {

using arch::x86_64::kLargePageSize;
using arch::x86_64::kMapNoExec;
using arch::x86_64::kMapWrite;
using arch::x86_64::kPageSize;
using arch::x86_64::Serial;

constexpr uint64_t kKernelStackSize = 64 * 1024;
constexpr uint64_t k4GiB = 4ull << 30;
constexpr size_t kMemoryMapSlack = 16;

efi::SystemTable* g_st = nullptr;
efi::BootServices* g_bs = nullptr;
bool g_boot_services_open = true;

// --- вывод -----------------------------------------------------------------

void con_out(const char* s) {
  if (!g_boot_services_open || g_st == nullptr || g_st->con_out == nullptr) return;
  char16_t buf[128];
  size_t n = 0;
  for (; *s != '\0'; ++s) {
    if (*s == '\n') buf[n++] = u'\r';
    buf[n++] = static_cast<char16_t>(static_cast<unsigned char>(*s));
    if (n >= 126) {
      buf[n] = 0;
      g_st->con_out->output_string(g_st->con_out, buf);
      n = 0;
    }
  }
  buf[n] = 0;
  g_st->con_out->output_string(g_st->con_out, buf);
}

// Обычный журнал — только serial: OVMF и так дублирует ConOut в serial.
// Ошибки (fail) дополнительно идут на экран через ConOut.
void log(const char* s) { Serial::write(s); }

void log_hex(const char* label, uint64_t v) {
  char buf[lib::core::kHex64Len + 1];
  size_t n = lib::core::format_hex64(buf, sizeof buf, v);
  buf[n] = '\0';
  log(label);
  log(buf);
  log("\n");
}

void log_dec(const char* label, uint64_t v) {
  char buf[21];
  size_t n = lib::core::format_dec(buf, sizeof buf - 1, v);
  buf[n] = '\0';
  log(label);
  log(buf);
  log("\n");
}

[[noreturn]] void fail(const char* what, uint64_t status = 0) {
  log("CharmOS boot: ERROR: ");
  log(what);
  log("\n");
  if (status != 0) log_hex("  status=", status);
  // Serial при отладке на железе может быть недоступен — дублируем на экран.
  con_out("CharmOS boot: ERROR: ");
  con_out(what);
  con_out("\n");
  arch::x86_64::qemu_exit(arch::x86_64::kExitFailure);
}

void check(efi::Status s, const char* what) {
  if (efi::is_error(s)) fail(what, s);
}

// --- память ----------------------------------------------------------------

void zero(uint64_t phys, uint64_t size) {
  auto* p = reinterpret_cast<uint8_t*>(phys);
  for (uint64_t i = 0; i < size; ++i) p[i] = 0;
}

void copy(uint64_t dst, const void* src, uint64_t size) {
  auto* d = reinterpret_cast<uint8_t*>(dst);
  const auto* s = static_cast<const uint8_t*>(src);
  for (uint64_t i = 0; i < size; ++i) d[i] = s[i];
}

uint64_t alloc_pages(efi::MemoryType type, uint64_t pages, const char* what) {
  efi::PhysicalAddress addr = 0;
  check(g_bs->allocate_pages(efi::AllocateType::AnyPages, type, pages, &addr), what);
  zero(addr, pages * kPageSize);
  return addr;
}

uint64_t alloc_table_page(void*) {
  efi::PhysicalAddress addr = 0;
  if (efi::is_error(g_bs->allocate_pages(efi::AllocateType::AnyPages,
                                         efi::MemoryType::OsBootloaderReclaimable, 1, &addr))) {
    return 0;
  }
  zero(addr, kPageSize);
  return addr;
}

constexpr uint64_t pages_for(uint64_t bytes) {
  return (bytes + kPageSize - 1) / kPageSize;
}

// --- файлы -----------------------------------------------------------------

struct FileData {
  uint64_t phys = 0;  // 0 — файла нет
  uint64_t size = 0;
};

// Читает файл целиком в страницы типа type (выравнивание 4 КиБ).
// Отсутствие файла не ошибка, если required == false.
FileData read_file(efi::FileProtocol* root, const char16_t* path, efi::MemoryType type,
                   bool required, const char* name) {
  efi::FileProtocol* file = nullptr;
  efi::Status s = root->open(root, &file, path, efi::kFileModeRead, 0);
  if (s == efi::kNotFound && !required) return {};
  check(s, name);

  alignas(8) uint8_t info_buf[512];
  uint64_t info_size = sizeof info_buf;
  check(file->get_info(file, &efi::kFileInfoId, &info_size, info_buf), "GetInfo");
  uint64_t size = reinterpret_cast<const efi::FileInfo*>(info_buf)->file_size;

  // +1 страница: место под завершающий 0 для текстовых файлов.
  uint64_t phys = alloc_pages(type, pages_for(size) + 1, "AllocatePages(file)");
  uint64_t done = 0;
  while (done < size) {
    uint64_t chunk = size - done;
    check(file->read(file, &chunk, reinterpret_cast<void*>(phys + done)), "Read");
    if (chunk == 0) fail("unexpected end of file");
    done += chunk;
  }
  file->close(file);
  return {phys, size};
}

// --- процессор -------------------------------------------------------------

uint64_t read_cr0() { uint64_t v; asm volatile("mov %%cr0, %0" : "=r"(v)); return v; }
uint64_t read_cr4() { uint64_t v; asm volatile("mov %%cr4, %0" : "=r"(v)); return v; }
void write_cr0(uint64_t v) { asm volatile("mov %0, %%cr0" : : "r"(v) : "memory"); }
void write_cr4(uint64_t v) { asm volatile("mov %0, %%cr4" : : "r"(v) : "memory"); }

uint64_t rdmsr(uint32_t msr) {
  uint32_t lo, hi;
  asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
  return (uint64_t{hi} << 32) | lo;
}

void wrmsr(uint32_t msr, uint64_t v) {
  asm volatile("wrmsr"
               :
               : "c"(msr), "a"(static_cast<uint32_t>(v)), "d"(static_cast<uint32_t>(v >> 32)));
}

uint64_t rdtsc() {
  uint32_t lo, hi;
  asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return (uint64_t{hi} << 32) | lo;
}

// SDM Vol. 3A §2.5 (CR0, CR4) и §2.2.1 (IA32_EFER).
constexpr uint64_t kCr0Wp = 1ull << 16;
constexpr uint64_t kCr4Osfxsr = 1ull << 9;
constexpr uint64_t kCr4Osxmmexcpt = 1ull << 10;
constexpr uint32_t kMsrEfer = 0xc0000080;
constexpr uint64_t kEferNxe = 1ull << 11;

// Плоская GDT (SDM Vol. 3A §3.4.5): 0x08 — 64-битный код (L=1), 0x10 — данные.
alignas(16) constinit uint64_t g_gdt[3] = {
    0,
    0x00af9a000000ffffull,
    0x00cf92000000ffffull,
};

struct [[gnu::packed]] Gdtr {
  uint16_t limit;
  uint64_t base;
};
constinit Gdtr g_gdtr = {sizeof g_gdt - 1, 0};

// --- загрузка ядра ---------------------------------------------------------

struct KernelImage {
  uint64_t entry = 0;
  uint64_t phys_base = ~uint64_t{0};
  uint64_t phys_size = 0;
};

KernelImage load_kernel(const FileData& file, arch::x86_64::PageTables& pt) {
  lib::elf::Image img;
  lib::elf::Error err = lib::elf::parse_kernel_image(reinterpret_cast<const void*>(file.phys),
                                                     file.size, abi::boot::kKernelBase, &img);
  if (err != lib::elf::Error::None) fail(lib::elf::error_name(err));

  KernelImage k;
  k.entry = img.entry;
  for (uint16_t i = 0; i < img.phnum; ++i) {
    const lib::elf::Elf64Phdr& p = img.phdrs[i];
    if (p.p_type != lib::elf::kPtLoad || p.p_memsz == 0) continue;
    uint64_t v0 = p.p_vaddr & ~(kPageSize - 1);
    uint64_t v1 = (p.p_vaddr + p.p_memsz + kPageSize - 1) & ~(kPageSize - 1);
    uint64_t pages = (v1 - v0) / kPageSize;
    uint64_t phys = alloc_pages(efi::MemoryType::OsKernelImage, pages, "AllocatePages(kernel)");
    copy(phys + (p.p_vaddr - v0), reinterpret_cast<const void*>(file.phys + p.p_offset),
         p.p_filesz);

    uint64_t flags = 0;
    if ((p.p_flags & lib::elf::kPfW) != 0) flags |= kMapWrite;
    if ((p.p_flags & lib::elf::kPfX) == 0) flags |= kMapNoExec;
    for (uint64_t j = 0; j < pages; ++j) {
      if (!pt.map_4k(v0 + j * kPageSize, phys + j * kPageSize, flags)) {
        fail("map kernel segment");
      }
    }
    if (phys < k.phys_base) k.phys_base = phys;
    k.phys_size += pages * kPageSize;
  }
  return k;
}

uint64_t find_rsdp() {
  for (uint64_t i = 0; i < g_st->number_of_table_entries; ++i) {
    const efi::ConfigurationTable& t = g_st->configuration_table[i];
    if (efi::guid_equal(t.vendor_guid, efi::kAcpi20TableGuid)) {
      return reinterpret_cast<uint64_t>(t.vendor_table);
    }
  }
  return 0;
}

abi::boot::Framebuffer find_framebuffer() {
  abi::boot::Framebuffer fb = {};
  void* iface = nullptr;
  if (efi::is_error(g_bs->locate_protocol(&efi::kGraphicsOutputProtocolGuid, nullptr, &iface))) {
    return fb;
  }
  const auto* gop = static_cast<efi::GraphicsOutputProtocol*>(iface);
  const efi::GraphicsOutputModeInformation* info = gop->mode->info;
  switch (info->pixel_format) {
    case efi::GraphicsPixelFormat::RedGreenBlueReserved8BitPerColor:
      fb.format = abi::boot::PixelFormat::Rgbx8888;
      break;
    case efi::GraphicsPixelFormat::BlueGreenRedReserved8BitPerColor:
      fb.format = abi::boot::PixelFormat::Bgrx8888;
      break;
    default:
      return fb;  // BitMask/BltOnly в v1 не поддерживаем: фреймбуфера нет
  }
  fb.base = gop->mode->frame_buffer_base;
  fb.width = info->horizontal_resolution;
  fb.height = info->vertical_resolution;
  fb.pitch = info->pixels_per_scan_line * 4;
  return fb;
}

// Верхняя граница прямого отображения: конец последнего региона,
// кроме Reserved выше 4 ГиБ (docs/abi/boot.md §4), с округлением до 2 МиБ.
uint64_t hhdm_top(const uint8_t* map, uint64_t count, uint64_t desc_size,
                  const abi::boot::Framebuffer& fb) {
  uint64_t top = k4GiB;
  for (uint64_t i = 0; i < count; ++i) {
    const auto* d = reinterpret_cast<const efi::MemoryDescriptor*>(map + i * desc_size);
    uint64_t end = d->physical_start + d->number_of_pages * kPageSize;
    bool reserved =
        boot::convert_memory_type(d->type) == abi::boot::MemoryType::Reserved && end > k4GiB;
    if (!reserved && end > top) top = end;
  }
  uint64_t fb_end = fb.base + uint64_t{fb.pitch} * fb.height;
  if (fb.base != 0 && fb_end > top) top = fb_end;
  return (top + kLargePageSize - 1) & ~(kLargePageSize - 1);
}

}  // namespace

extern "C" uint64_t efi_main(efi::Handle image_handle, efi::SystemTable* st) {
  g_st = st;
  g_bs = st->boot_services;
  Serial::init();
  log("CharmOS boot: loader started\n");
  g_bs->set_watchdog_timer(0, 0, 0, nullptr);

  void* iface = nullptr;
  check(g_bs->handle_protocol(image_handle, &efi::kLoadedImageProtocolGuid, &iface),
        "LoadedImage");
  const auto* loaded = static_cast<efi::LoadedImageProtocol*>(iface);
  uint64_t image_end = reinterpret_cast<uint64_t>(loaded->image_base) + loaded->image_size;
  if (image_end > k4GiB) fail("loader image above 4 GiB");

  check(g_bs->handle_protocol(loaded->device_handle, &efi::kSimpleFileSystemProtocolGuid, &iface),
        "SimpleFileSystem");
  auto* fs = static_cast<efi::SimpleFileSystemProtocol*>(iface);
  efi::FileProtocol* root = nullptr;
  check(fs->open_volume(fs, &root), "OpenVolume");

  FileData kernel_file = read_file(root, u"\\charmos\\kernel.elf", efi::MemoryType::LoaderData,
                                   true, "open \\charmos\\kernel.elf");
  log_dec("CharmOS boot: kernel.elf bytes=", kernel_file.size);
  FileData cmdline = read_file(root, u"\\charmos\\cmdline.txt",
                               efi::MemoryType::OsBootloaderReclaimable, false, "cmdline.txt");
  if (cmdline.phys != 0) {
    auto* text = reinterpret_cast<char*>(cmdline.phys);
    while (cmdline.size > 0 &&
           (text[cmdline.size - 1] == '\n' || text[cmdline.size - 1] == '\r')) {
      --cmdline.size;
    }
    text[cmdline.size] = '\0';
  }

  arch::x86_64::PageTables pt;
  if (!pt.init(&alloc_table_page, nullptr, 0)) fail("page table allocation");
  KernelImage kernel = load_kernel(kernel_file, pt);
  log_hex("CharmOS boot: kernel entry=", kernel.entry);

  abi::boot::Framebuffer fb = find_framebuffer();
  uint64_t rsdp = find_rsdp();

  uint64_t stack = alloc_pages(efi::MemoryType::OsBootloaderReclaimable,
                               kKernelStackSize / kPageSize, "AllocatePages(stack)");
  uint64_t info_phys = alloc_pages(efi::MemoryType::OsBootloaderReclaimable,
                                   pages_for(sizeof(abi::boot::BootInfo)), "AllocatePages(info)");

  // Первый снимок карты: только чтобы узнать размеры и верхнюю границу памяти.
  uint64_t map_size = 0, map_key = 0, desc_size = 0;
  uint32_t desc_version = 0;
  efi::Status s = g_bs->get_memory_map(&map_size, nullptr, &map_key, &desc_size, &desc_version);
  if (s != efi::kBufferTooSmall) fail("GetMemoryMap(size)", s);
  // Запас: выделения ниже добавляют дескрипторы.
  uint64_t map_cap = map_size + kMemoryMapSlack * desc_size;
  uint64_t efi_map = alloc_pages(efi::MemoryType::LoaderData, pages_for(map_cap),
                                 "AllocatePages(efi map)");
  map_size = map_cap;
  check(g_bs->get_memory_map(&map_size, reinterpret_cast<efi::MemoryDescriptor*>(efi_map),
                             &map_key, &desc_size, &desc_version),
        "GetMemoryMap");

  uint64_t top = hhdm_top(reinterpret_cast<const uint8_t*>(efi_map), map_size / desc_size,
                          desc_size, fb);
  if (!pt.map_range(abi::boot::kHhdmBase, 0, top, kMapWrite | kMapNoExec)) {
    fail("map HHDM");
  }
  if (!pt.map_range(0, 0, k4GiB, kMapWrite)) fail("map identity 4 GiB");

  uint64_t region_cap = map_cap / desc_size + kMemoryMapSlack;
  uint64_t regions = alloc_pages(efi::MemoryType::OsBootloaderReclaimable,
                                 pages_for(region_cap * sizeof(abi::boot::MemoryRegion)),
                                 "AllocatePages(regions)");

  log_hex("CharmOS boot: hhdm top=", top);
  log("CharmOS boot: exiting boot services\n");

  // С этого места — никаких выделений памяти: они меняют map_key.
  for (int attempt = 0;; ++attempt) {
    map_size = map_cap;
    s = g_bs->get_memory_map(&map_size, reinterpret_cast<efi::MemoryDescriptor*>(efi_map),
                             &map_key, &desc_size, &desc_version);
    if (efi::is_error(s)) fail("GetMemoryMap(final)", s);
    s = g_bs->exit_boot_services(image_handle, map_key);
    if (!efi::is_error(s)) break;
    if (attempt == 1) fail("ExitBootServices", s);
  }
  g_boot_services_open = false;

  size_t count = boot::convert_memory_map(
      reinterpret_cast<const uint8_t*>(efi_map), map_size / desc_size, desc_size, fb.base,
      fb.base != 0 ? uint64_t{fb.pitch} * fb.height : 0,
      reinterpret_cast<abi::boot::MemoryRegion*>(regions), region_cap);
  if (count == 0) fail("memory map conversion");

  auto* info = reinterpret_cast<abi::boot::BootInfo*>(info_phys);
  info->magic = abi::boot::kMagic;
  info->version_major = abi::boot::kVersionMajor;
  info->version_minor = abi::boot::kVersionMinor;
  info->size = sizeof(abi::boot::BootInfo);
  info->flags = abi::boot::kFlagIdentityMapLow4G;
  info->memory_map = regions;
  info->memory_map_count = count;
  info->kernel_phys_base = kernel.phys_base;
  info->kernel_phys_size = kernel.phys_size;
  info->hhdm_base = abi::boot::kHhdmBase;
  info->framebuffer = fb;
  info->acpi_rsdp = rsdp;
  info->efi_system_table = reinterpret_cast<uint64_t>(st);
  info->cmdline = cmdline.phys;
  info->cmdline_length = cmdline.size;

  asm volatile("cli");
  wrmsr(kMsrEfer, rdmsr(kMsrEfer) | kEferNxe);
  write_cr0(read_cr0() | kCr0Wp);
  write_cr4(read_cr4() | kCr4Osfxsr | kCr4Osxmmexcpt);

  g_gdtr.base = abi::boot::kHhdmBase + reinterpret_cast<uint64_t>(&g_gdt);
  info->boot_time_tsc = rdtsc();
  enter_kernel(pt.root(), &g_gdtr, abi::boot::kHhdmBase + stack + kKernelStackSize, kernel.entry,
               info_phys);
}
