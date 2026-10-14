#pragma once
// Типы UEFI, нужные загрузчику. Источник — docs/specs/uefi-2.11.pdf;
// раздел указан у каждой структуры. Функции, которые мы не вызываем,
// описаны как void* только для сохранения раскладки таблиц.
// Соглашение о вызовах — MS x64 (цель x86_64-unknown-windows), EFIAPI не нужен.

#include <stddef.h>
#include <stdint.h>

namespace efi {

using Status = uint64_t;
using Handle = void*;
using PhysicalAddress = uint64_t;
using VirtualAddress = uint64_t;
using Char16 = char16_t;

// Приложение D: у кодов ошибок установлен старший бит.
inline constexpr Status kErrorBit = 1ull << 63;
inline constexpr Status kSuccess = 0;
inline constexpr Status kInvalidParameter = kErrorBit | 2;
inline constexpr Status kBufferTooSmall = kErrorBit | 5;
inline constexpr Status kNotFound = kErrorBit | 14;
constexpr bool is_error(Status s) { return (s & kErrorBit) != 0; }

struct Guid {
  uint32_t data1;
  uint16_t data2;
  uint16_t data3;
  uint8_t data4[8];
};
static_assert(sizeof(Guid) == 16);

inline bool guid_equal(const Guid& a, const Guid& b) {
  const auto* x = reinterpret_cast<const uint8_t*>(&a);
  const auto* y = reinterpret_cast<const uint8_t*>(&b);
  for (size_t i = 0; i < sizeof(Guid); ++i) {
    if (x[i] != y[i]) return false;
  }
  return true;
}

// §9.1
inline constexpr Guid kLoadedImageProtocolGuid = {
    0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
// §13.4
inline constexpr Guid kSimpleFileSystemProtocolGuid = {
    0x0964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
// §13.5.16
inline constexpr Guid kFileInfoId = {
    0x09576e92, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
// §12.9
inline constexpr Guid kGraphicsOutputProtocolGuid = {
    0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};
// §4.6
inline constexpr Guid kAcpi20TableGuid = {
    0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}};

// §4.2
struct TableHeader {
  uint64_t signature;
  uint32_t revision;
  uint32_t header_size;
  uint32_t crc32;
  uint32_t reserved;
};
static_assert(sizeof(TableHeader) == 24);

// §7.2
enum class AllocateType : uint32_t { AnyPages = 0, MaxAddress = 1, Address = 2 };

enum class MemoryType : uint32_t {
  Reserved = 0,
  LoaderCode = 1,
  LoaderData = 2,
  BootServicesCode = 3,
  BootServicesData = 4,
  RuntimeServicesCode = 5,
  RuntimeServicesData = 6,
  Conventional = 7,
  Unusable = 8,
  AcpiReclaim = 9,
  AcpiNvs = 10,
  MemoryMappedIo = 11,
  MemoryMappedIoPortSpace = 12,
  PalCode = 13,
  Persistent = 14,
  Unaccepted = 15,
  // 0x80000000..0xFFFFFFFF — для загрузчиков ОС (§7.2, AllocatePages).
  OsKernelImage = 0x80000001,
  OsBootloaderReclaimable = 0x80000002,
  OsInitrd = 0x80000003,
};

// §7.2, GetMemoryMap. Шаг между дескрипторами — DescriptorSize, не sizeof.
struct MemoryDescriptor {
  uint32_t type;
  uint32_t pad;
  PhysicalAddress physical_start;
  VirtualAddress virtual_start;
  uint64_t number_of_pages;
  uint64_t attribute;
};
static_assert(sizeof(MemoryDescriptor) == 40);
static_assert(offsetof(MemoryDescriptor, physical_start) == 8);
static_assert(offsetof(MemoryDescriptor, number_of_pages) == 24);

// §12.4
struct SimpleTextOutputProtocol {
  void* reset;
  Status (*output_string)(SimpleTextOutputProtocol* self, const Char16* string);
  void* test_string;
  void* query_mode;
  void* set_mode;
  void* set_attribute;
  void* clear_screen;
  void* set_cursor_position;
  void* enable_cursor;
  void* mode;
};
static_assert(offsetof(SimpleTextOutputProtocol, output_string) == 8);

// §4.4
struct BootServices {
  TableHeader hdr;
  void* raise_tpl;
  void* restore_tpl;
  Status (*allocate_pages)(AllocateType type, MemoryType memory_type, uint64_t pages,
                           PhysicalAddress* memory);
  Status (*free_pages)(PhysicalAddress memory, uint64_t pages);
  Status (*get_memory_map)(uint64_t* memory_map_size, MemoryDescriptor* memory_map,
                           uint64_t* map_key, uint64_t* descriptor_size,
                           uint32_t* descriptor_version);
  Status (*allocate_pool)(MemoryType pool_type, uint64_t size, void** buffer);
  Status (*free_pool)(void* buffer);
  void* create_event;
  void* set_timer;
  void* wait_for_event;
  void* signal_event;
  void* close_event;
  void* check_event;
  void* install_protocol_interface;
  void* reinstall_protocol_interface;
  void* uninstall_protocol_interface;
  Status (*handle_protocol)(Handle handle, const Guid* protocol, void** interface);
  void* reserved;
  void* register_protocol_notify;
  void* locate_handle;
  void* locate_device_path;
  void* install_configuration_table;
  void* load_image;
  void* start_image;
  void* exit;
  void* unload_image;
  Status (*exit_boot_services)(Handle image_handle, uint64_t map_key);
  void* get_next_monotonic_count;
  void* stall;
  Status (*set_watchdog_timer)(uint64_t timeout, uint64_t watchdog_code, uint64_t data_size,
                               const Char16* watchdog_data);
  void* connect_controller;
  void* disconnect_controller;
  void* open_protocol;
  void* close_protocol;
  void* open_protocol_information;
  void* protocols_per_handle;
  void* locate_handle_buffer;
  Status (*locate_protocol)(const Guid* protocol, void* registration, void** interface);
  void* install_multiple_protocol_interfaces;
  void* uninstall_multiple_protocol_interfaces;
  void* calculate_crc32;
  void* copy_mem;
  void* set_mem;
  void* create_event_ex;
};
static_assert(offsetof(BootServices, allocate_pages) == 24 + 2 * 8);
static_assert(offsetof(BootServices, handle_protocol) == 24 + 16 * 8);
static_assert(offsetof(BootServices, exit_boot_services) == 24 + 26 * 8);
static_assert(offsetof(BootServices, set_watchdog_timer) == 24 + 29 * 8);
static_assert(offsetof(BootServices, locate_protocol) == 24 + 37 * 8);
static_assert(sizeof(BootServices) == 24 + 44 * 8);

// §4.6
struct ConfigurationTable {
  Guid vendor_guid;
  void* vendor_table;
};
static_assert(sizeof(ConfigurationTable) == 24);

// §4.3
struct SystemTable {
  TableHeader hdr;
  Char16* firmware_vendor;
  uint32_t firmware_revision;
  Handle console_in_handle;
  void* con_in;
  Handle console_out_handle;
  SimpleTextOutputProtocol* con_out;
  Handle standard_error_handle;
  SimpleTextOutputProtocol* std_err;
  void* runtime_services;
  BootServices* boot_services;
  uint64_t number_of_table_entries;
  ConfigurationTable* configuration_table;
};
static_assert(offsetof(SystemTable, con_out) == 64);
static_assert(offsetof(SystemTable, boot_services) == 96);
static_assert(offsetof(SystemTable, configuration_table) == 112);

// §9.1
struct LoadedImageProtocol {
  uint32_t revision;
  Handle parent_handle;
  SystemTable* system_table;
  Handle device_handle;
  void* file_path;
  void* reserved;
  uint32_t load_options_size;
  void* load_options;
  void* image_base;
  uint64_t image_size;
  MemoryType image_code_type;
  MemoryType image_data_type;
  void* unload;
};
static_assert(offsetof(LoadedImageProtocol, device_handle) == 24);
static_assert(offsetof(LoadedImageProtocol, image_base) == 64);
static_assert(offsetof(LoadedImageProtocol, image_size) == 72);

// §13.5
inline constexpr uint64_t kFileModeRead = 0x1;

struct FileProtocol {
  uint64_t revision;
  Status (*open)(FileProtocol* self, FileProtocol** new_handle, const Char16* file_name,
                 uint64_t open_mode, uint64_t attributes);
  Status (*close)(FileProtocol* self);
  void* delete_file;
  Status (*read)(FileProtocol* self, uint64_t* buffer_size, void* buffer);
  void* write;
  void* get_position;
  void* set_position;
  Status (*get_info)(FileProtocol* self, const Guid* information_type, uint64_t* buffer_size,
                     void* buffer);
  void* set_info;
  void* flush;
  void* open_ex;
  void* read_ex;
  void* write_ex;
  void* flush_ex;
};
static_assert(offsetof(FileProtocol, read) == 32);
static_assert(offsetof(FileProtocol, get_info) == 64);

// §8.3 (EFI_TIME) и §13.5.16 (EFI_FILE_INFO)
struct Time {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t pad1;
  uint32_t nanosecond;
  int16_t time_zone;
  uint8_t daylight;
  uint8_t pad2;
};
static_assert(sizeof(Time) == 16);

struct FileInfo {
  uint64_t size;
  uint64_t file_size;
  uint64_t physical_size;
  Time create_time;
  Time last_access_time;
  Time modification_time;
  uint64_t attribute;
  // Далее CHAR16 FileName[] переменной длины.
};
static_assert(offsetof(FileInfo, file_size) == 8);
static_assert(sizeof(FileInfo) == 80);

// §13.4
struct SimpleFileSystemProtocol {
  uint64_t revision;
  Status (*open_volume)(SimpleFileSystemProtocol* self, FileProtocol** root);
};

// §12.9
enum class GraphicsPixelFormat : uint32_t {
  RedGreenBlueReserved8BitPerColor = 0,
  BlueGreenRedReserved8BitPerColor = 1,
  BitMask = 2,
  BltOnly = 3,
};

struct PixelBitmask {
  uint32_t red_mask;
  uint32_t green_mask;
  uint32_t blue_mask;
  uint32_t reserved_mask;
};

struct GraphicsOutputModeInformation {
  uint32_t version;
  uint32_t horizontal_resolution;
  uint32_t vertical_resolution;
  GraphicsPixelFormat pixel_format;
  PixelBitmask pixel_information;
  uint32_t pixels_per_scan_line;
};
static_assert(sizeof(GraphicsOutputModeInformation) == 36);

struct GraphicsOutputProtocolMode {
  uint32_t max_mode;
  uint32_t mode;
  GraphicsOutputModeInformation* info;
  uint64_t size_of_info;
  PhysicalAddress frame_buffer_base;
  uint64_t frame_buffer_size;
};
static_assert(offsetof(GraphicsOutputProtocolMode, frame_buffer_base) == 24);

struct GraphicsOutputProtocol {
  void* query_mode;
  void* set_mode;
  void* blt;
  GraphicsOutputProtocolMode* mode;
};

}  // namespace efi
