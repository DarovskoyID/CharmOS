#pragma once

#include <abi/boot.hpp>

namespace kernel {

// Проверка BootInfo по docs/abi/boot.md §6. nullptr — всё в порядке,
// иначе — описание ошибки.
const char* check_boot_info(const abi::boot::BootInfo& info);

}  // namespace kernel
