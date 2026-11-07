#include "ps2.hpp"

#include <abi/syscall.hpp>
#include <arch/x86_64/cpu.hpp>
#include <arch/x86_64/i8042.hpp>
#include <lib/acpi/tables.hpp>
#include <lib/input/ps2.hpp>

#include "../core/log.hpp"
#include "../ipc/ipc.hpp"
#include "../platform/platform.hpp"
#include "../smp/smp.hpp"

namespace kernel::input {

namespace k = arch::x86_64::i8042;

namespace {

// Ответы устройств (IBM HITR, клавиатура: ACK FA; мышь — docs/specs/ps2-mouse-sources.md,
// источник вторичный).
constexpr uint8_t kAck = 0xfa;
constexpr uint8_t kKbdSelectScanSet = 0xf0, kKbdEnable = 0xf4;
constexpr uint8_t kMouseSetDefaults = 0xf6, kMouseEnableReporting = 0xf4;
// Пакет мыши (там же): байт 1 — YO XO YS XS 1 MB RB LB, затем dx, dy; y растёт вверх.
constexpr uint8_t kMouseAlways1 = 1u << 3, kMouseXSign = 1u << 4, kMouseYSign = 1u << 5;
constexpr uint8_t kMouseXOverflow = 1u << 6, kMouseYOverflow = 1u << 7;

constinit lib::input::Set2Decoder g_kbd = {};
constinit uint8_t g_mouse[4] = {};
constinit uint8_t g_mouse_len = 0;
constinit uint8_t g_mouse_size = 3;  // 4 при включённом колесе (Intellimouse)
constinit uint8_t g_buttons = 0;

// «Писать в 60h/64h только при пустых буферах» (Programming Considerations).
bool wait_input_empty() {
  for (uint64_t deadline = platform::now_ns() + 100'000'000; platform::now_ns() < deadline;) {
    if ((k::status() & k::kStatusInputFull) == 0) return true;
    arch::x86_64::pause();
  }
  return false;
}

bool read_byte(uint8_t* out, uint64_t timeout_ns) {
  for (uint64_t deadline = platform::now_ns() + timeout_ns; platform::now_ns() < deadline;) {
    if ((k::status() & k::kStatusOutputFull) != 0) {
      *out = k::read_data();
      return true;
    }
    arch::x86_64::pause();
  }
  return false;
}

bool command(uint8_t c) { return wait_input_empty() && (k::write_command(c), true); }
bool data(uint8_t d) { return wait_input_empty() && (k::write_data(d), true); }

// Байт клавиатуре и ACK (до включения прерываний — опросом).
bool kbd_command(uint8_t c) {
  uint8_t r;
  return data(c) && read_byte(&r, 200'000'000) && r == kAck;
}

// Байт мыши через D4 и ожидание ACK (до включения прерываний — опросом).
bool mouse_command(uint8_t c) {
  uint8_t r;
  return command(k::kWriteAux) && data(c) && read_byte(&r, 200'000'000) && r == kAck;
}

void push(uint16_t type, uint16_t code, int32_t value, int32_t x, int32_t y) {
  abi::syscall::InputEvent e{platform::now_ns(), type, code, value, x, y};
  ipc::input_push(e);
}

void mouse_packet() {
  uint8_t f = g_mouse[0];
  if ((f & (kMouseXOverflow | kMouseYOverflow)) == 0) {
    // 9-битное дополнение до двух: знак — в первом байте.
    int32_t dx = static_cast<int32_t>(g_mouse[1]) - ((f & kMouseXSign) != 0 ? 256 : 0);
    int32_t dy = static_cast<int32_t>(g_mouse[2]) - ((f & kMouseYSign) != 0 ? 256 : 0);
    if (dx != 0 || dy != 0) push(abi::syscall::kInputMotion, 0, 0, dx, -dy);  // экран: вниз +
  }
  uint8_t buttons = f & 7;  // LB, RB, MB
  static constexpr uint16_t kCode[3] = {1, 2, 3};  // input.md: 1 левая, 2 правая, 3 средняя
  for (int i = 0; i < 3; ++i) {
    if (((buttons ^ g_buttons) >> i & 1) != 0) {
      push(abi::syscall::kInputButton, kCode[i], (buttons >> i & 1) != 0 ? 1 : 0, 0, 0);
    }
  }
  g_buttons = buttons;
  if (g_mouse_size == 4) {
    // IntelliMouse: байт 4 — Z (колесо), 4-битное дополнение до двух,
    // плюс состояния 4-й/5-й кнопок. Вверх — положительное значение.
    int32_t dz = static_cast<int32_t>(g_mouse[3] & 0x0f) -
                 ((g_mouse[3] & 0x08) != 0 ? 16 : 0);
    // Соглашение input.md/UI: колесо «вверх» — положительное значение.
    if (dz != 0) push(abi::syscall::kInputWheel, 0, -dz, 0, 0);
  }
}

void irq_handler(arch::x86_64::InterruptFrame&) {
  for (int n = 0; n < 32; ++n) {
    uint8_t st = k::status();
    if ((st & k::kStatusOutputFull) == 0) break;
    uint8_t b = k::read_data();
    if ((st & (k::kStatusParity | k::kStatusTimeout)) != 0) continue;  // FF/FE — ошибка линии
    if ((st & k::kStatusAuxData) != 0) {
      // Ресинхронизация: первый байт пакета всегда с битом 3.
      if (g_mouse_len == 0 && (b & kMouseAlways1) == 0) continue;
      g_mouse[g_mouse_len++] = b;
      if (g_mouse_len == g_mouse_size) {
        g_mouse_len = 0;
        mouse_packet();
      }
    } else {
      lib::input::KeyEvent ke{};
      if (g_kbd.feed(b, &ke)) push(abi::syscall::kInputKey, ke.usage, ke.pressed ? 1 : 0, 0, 0);
    }
  }
  platform::local_apic().eoi();
}

// ISA IRQ → GSI через Interrupt Source Override (как у COM1, console.cpp).
bool route(uint8_t isa, uint8_t vector) {
  uint32_t gsi = isa;
  bool level = false, low = false;
  const auto& m = platform::madt();
  for (size_t i = 0; i < m.override_count; ++i) {
    if (m.overrides[i].source != isa) continue;
    gsi = m.overrides[i].gsi;
    low = (m.overrides[i].flags & lib::acpi::kIntiPolarityMask) == lib::acpi::kIntiPolarityLow;
    level = (m.overrides[i].flags & lib::acpi::kIntiTriggerMask) == lib::acpi::kIntiTriggerLevel;
  }
  const auto* io = platform::io_apic_for(gsi);
  if (io == nullptr) return false;
  io->route(gsi, {vector, level, low, static_cast<uint8_t>(smp::cpu(0).apic_id)});
  return true;
}

}  // namespace

bool ps2_init() {
  if (k::status() == 0xff) {  // шина ничего не возвращает — контроллера нет
    kprint("ps2: no 8042 controller\n");
    return false;
  }
  // Устройства выключены, буфер пуст, затем командный байт: без трансляции (набор 2), без IRQ.
  if (!command(k::kDisableKbd) || !command(k::kDisableAux)) return false;
  for (uint8_t junk; read_byte(&junk, 5'000'000);) {
  }
  uint8_t cb;
  if (!command(k::kReadCmdByte) || !read_byte(&cb, 100'000'000)) {
    kprint("ps2: controller does not answer\n");
    return false;
  }
  cb = static_cast<uint8_t>(cb & ~(k::kCmdByteTranslate | k::kCmdByteKbdIrq | k::kCmdByteAuxIrq |
                                   k::kCmdByteKbdDisable | k::kCmdByteAuxDisable));
  if (!command(k::kWriteCmdByte) || !data(cb)) return false;
  if (!command(k::kEnableKbd)) return false;
  // Прошивка могла оставить клавиатуру без сканирования или в другом наборе:
  // F0 02 — набор 2, F4 — сканировать (IBM HITR, «Keyboards», Commands).
  bool kbd = kbd_command(kKbdSelectScanSet) && kbd_command(2) && kbd_command(kKbdEnable);
  if (!kbd) kprint("ps2: keyboard did not acknowledge setup\n");
  bool mouse = command(k::kEnableAux) && mouse_command(kMouseSetDefaults);
  if (mouse) {
    // Intellimouse (колесо): «магическая» последовательность частот 200/100/80
    // переключает мышь на 4-байтные пакеты, затем F2 отдаёт id (3 = с колесом).
    // Простая 3-кнопочная мышь последовательность принимает, но id остаётся 0.
    const bool sequence = mouse_command(0xf3) && mouse_command(200) &&
                          mouse_command(0xf3) && mouse_command(100) &&
                          mouse_command(0xf3) && mouse_command(80) &&
                          mouse_command(0xf2);
    if (sequence) {
      uint8_t id = 0;
      if (read_byte(&id, 200'000'000) && id == 3) g_mouse_size = 4;
    }
    mouse = mouse_command(kMouseEnableReporting);
  }
  for (uint8_t junk; read_byte(&junk, 5'000'000);) {
  }
  uint8_t vector = platform::alloc_device_vector(&irq_handler);
  if (vector == 0 || !route(k::kIsaIrqKbd, vector) || (mouse && !route(k::kIsaIrqAux, vector))) {
    kprint("ps2: cannot route IRQs\n");
    return false;
  }
  // Байт прочитан, когда устройства были выключены (AD/A7): биты 4 и 5 сбросить,
  // иначе запись снова отключит их (рис. 5).
  cb = static_cast<uint8_t>((cb & ~(k::kCmdByteKbdDisable | k::kCmdByteAuxDisable)) | k::kCmdByteKbdIrq |
                            (mouse ? k::kCmdByteAuxIrq : 0));
  if (!mouse) cb = static_cast<uint8_t>(cb | k::kCmdByteAuxDisable);
  if (!command(k::kWriteCmdByte) || !data(cb)) return false;
  kprint("ps2: keyboard{} (scan set 2)\n", mouse ? " and mouse" : "");
  return true;
}

}  // namespace kernel::input
