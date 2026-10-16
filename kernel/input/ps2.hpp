#pragma once
// Клавиатура и мышь PS/2 в ядре (журнал решений 2026-10-15): события — в
// кольцо компоновщика (docs/abi/input.md).

namespace kernel::input {

// Инициализирует 8042, клавиатуру и мышь, включает IRQ 1 и 12. false — нет контроллера.
bool ps2_init();

}  // namespace kernel::input
