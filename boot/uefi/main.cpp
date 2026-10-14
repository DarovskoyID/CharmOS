// M0: заглушка UEFI-приложения. Проверяет весь путь сборка → ESP → OVMF →
// serial → isa-debug-exit. В M1 превращается в загрузчик.

#include <arch/x86_64/serial.hpp>
#include <lib/core/format.hpp>

namespace {

using arch::x86_64::Serial;

void print_hex(const char* label, uint64_t value) {
  char buf[lib::core::kHex64Len];
  size_t n = lib::core::format_hex64(buf, sizeof buf, value);
  Serial::write(label);
  Serial::write(buf, n);
  Serial::write("\n");
}

}  // namespace

// Типы UEFI (EFI_HANDLE, EFI_SYSTEM_TABLE) появятся в M1 вместе с docs/abi/boot.md.
extern "C" uint64_t efi_main(void* image_handle, void* system_table) {
  Serial::init();
  Serial::write("CharmOS M0: UEFI stub alive\n");
  print_hex("image_handle=", reinterpret_cast<uint64_t>(image_handle));
  print_hex("system_table=", reinterpret_cast<uint64_t>(system_table));
  Serial::write("CharmOS M0: OK\n");
  arch::x86_64::qemu_exit(arch::x86_64::kExitSuccess);
}
