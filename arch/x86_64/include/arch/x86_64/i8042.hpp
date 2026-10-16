#pragma once
// Контроллер клавиатуры и вспомогательного устройства 8042 (IBM PS/2 Hardware
// Interface Technical Reference — Common Interfaces, 1990, «Keyboard/Auxiliary
// Device Controller»): порты, биты статуса и командного байта, команды.

#include <stdint.h>

#include <arch/x86_64/io.hpp>

namespace arch::x86_64::i8042 {

inline constexpr uint16_t kData = 0x60;    // буфер вывода (чтение) / ввода (запись)
inline constexpr uint16_t kStatus = 0x64;  // статус (чтение) / команда (запись)

// Рис. 2: регистр статуса.
inline constexpr uint8_t kStatusOutputFull = 1u << 0;
inline constexpr uint8_t kStatusInputFull = 1u << 1;
inline constexpr uint8_t kStatusAuxData = 1u << 5;  // вместе с битом 0: данные мыши
inline constexpr uint8_t kStatusTimeout = 1u << 6;
inline constexpr uint8_t kStatusParity = 1u << 7;

// Рис. 5: командный байт контроллера.
inline constexpr uint8_t kCmdByteKbdIrq = 1u << 0;     // IRQ 1
inline constexpr uint8_t kCmdByteAuxIrq = 1u << 1;     // IRQ 12
inline constexpr uint8_t kCmdByteKbdDisable = 1u << 4;
inline constexpr uint8_t kCmdByteAuxDisable = 1u << 5;
inline constexpr uint8_t kCmdByteTranslate = 1u << 6;  // перевод в набор 1 (выключаем)

// Команды контроллера (порт 64h).
inline constexpr uint8_t kReadCmdByte = 0x20;
inline constexpr uint8_t kWriteCmdByte = 0x60;
inline constexpr uint8_t kDisableAux = 0xa7;
inline constexpr uint8_t kEnableAux = 0xa8;
inline constexpr uint8_t kDisableKbd = 0xad;
inline constexpr uint8_t kEnableKbd = 0xae;
inline constexpr uint8_t kWriteAux = 0xd4;  // следующий байт в 60h — мыши

inline constexpr uint8_t kIsaIrqKbd = 1;
inline constexpr uint8_t kIsaIrqAux = 12;

inline uint8_t status() { return inb(kStatus); }
inline uint8_t read_data() { return inb(kData); }
inline void write_command(uint8_t c) { outb(kStatus, c); }
inline void write_data(uint8_t d) { outb(kData, d); }

}  // namespace arch::x86_64::i8042
