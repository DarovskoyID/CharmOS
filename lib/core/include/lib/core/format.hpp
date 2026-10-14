#pragma once

#include <stddef.h>
#include <stdint.h>

namespace lib::core {

// Пишет value в buf как "0x" + 16 шестнадцатеричных цифр, без завершающего нуля.
// Возвращает число записанных символов или 0, если не хватает места.
inline constexpr size_t kHex64Len = 18;
size_t format_hex64(char* buf, size_t cap, uint64_t value);

// Пишет value в десятичном виде. Возвращает длину или 0, если не хватает места.
size_t format_dec(char* buf, size_t cap, uint64_t value);

}  // namespace lib::core
