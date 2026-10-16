#pragma once
// Клавиатура PS/2: поток скан-кодов набора 2 → события с HID Usage
// (docs/abi/input.md §3.1). Без зависимостей: ядро и хост-тесты.

#include <stdint.h>

namespace lib::input {

struct KeyEvent {
  uint16_t usage;  // HID Usage, страница Keyboard/Keypad
  bool pressed;
};

// Набор 2 (IBM PS/2 HITR 1988, клавиатуры 101/102): make — код (E0 — префикс
// дополнительных клавиш), break — F0 перед кодом. Автоповтор приходит как
// повторный make. Pause отпускания не имеет.
class Set2Decoder {
 public:
  // true — out заполнен.
  bool feed(uint8_t byte, KeyEvent* out);

 private:
  bool extended_ = false;
  bool release_ = false;
  uint8_t pause_left_ = 0;
};

}  // namespace lib::input
