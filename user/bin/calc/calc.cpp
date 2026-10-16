#include <charm/canvas.hpp>
#include <charm/window.hpp>
#include <charm/expression.hpp>
#include <charm/text_input.hpp>
#include <stdio.h>
#include <string.h>

namespace {
namespace sc = abi::syscall;
namespace wm = abi::wm;
constexpr int width = 860, height = 480;
extern "C" const uint8_t wm_ui_font[], wm_ui_font_end[], wm_mono_font[], wm_mono_font_end[];
charm::gui::Canvas canvas;
char expression[192] = "2+2";
unsigned length = 3, shift = 0;
bool graph = false, replace = true;
double parameter = 1;
void edit(uint32_t cp) {
  if (replace) { expression[0] = 0; length = 0; replace = false; }
  charm::ui::append(expression, sizeof expression, length, cp);
}
void format(char* out, size_t capacity, double value) {
  int exponent = 0;
  while (value > 1e9 || value < -1e9) { value /= 10; ++exponent; }
  while (value != 0 && value < 0.000001 && value > -0.000001 && exponent > -100) { value *= 10; --exponent; }
  if (exponent) snprintf(out, capacity, "%.6f ×10^%d", value, exponent);
  else snprintf(out, capacity, "%.6f", value);
}
void mode(bool plot) {
  graph = plot;
  snprintf(expression, sizeof expression, "%s", graph ? "a*sin(x)" : "2+2");
  length = static_cast<unsigned>(strlen(expression)); replace = true;
}
void line(int x0, int y0, int x1, int y1) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
  int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;
  for (;;) {
    canvas.fill(x0, y0, 2, 2, 0xf5c518);
    if (x0 == x1 && y0 == y1) break;
    int twice = error * 2;
    if (twice >= dy) { error += dy; x0 += sx; }
    if (twice <= dx) { error += dx; y0 += sy; }
  }
}
void draw(uint32_t* pixels, bool bgrx) {
  canvas.target(pixels, width, height, bgrx); canvas.fill(0, 0, width, height, 0x121214);
  canvas.rounded(20, 14, 180, 38, 9, graph ? 0x1b1a18 : 0x332e25);
  canvas.rounded(212, 14, 180, 38, 9, graph ? 0x332e25 : 0x1b1a18);
  canvas.text("Вычисления · F1", 34, 40, graph ? 0xb7b1a6 : 0xf5c518, 196);
  canvas.text("График · F2", 230, 40, graph ? 0xf5c518 : 0xb7b1a6, 386);
  canvas.rounded(20, 72, 820, 64, 12, 0x332e25);
  canvas.rounded(21, 73, 818, 62, 11, 0x0b0b0c);
  canvas.text(expression, 36, 114, 0xf3efe7, 824, 20, true);
  if (graph) {
    char label[96]; snprintf(label, sizeof label, "x: −10…10   y: −10…10   a = %.2f   ↑↓: параметр", parameter);
    canvas.text(label, 26, 164, 0xb7b1a6, 835, 12);
    canvas.fill(30, 184, 800, 240, 0x0b0b0c);
    for (int i = 0; i <= 20; ++i) {
      canvas.fill(30 + i * 40, 184, 1, 240, i == 10 ? 0x8e887d : 0x242426);
      canvas.fill(30, 184 + i * 12, 800, 1, i == 10 ? 0x8e887d : 0x242426);
    }
    bool previous = false; int px = 0, py = 0;
    for (int i = 0; i < 400; ++i) {
      double x = -10 + i * (20.0 / 399);
      auto r = charm::math::evaluate(expression, x, parameter);
      if (r.error != charm::math::Error::None || r.value < -10 || r.value > 10) { previous = false; continue; }
      int xx = 30 + i * 2, yy = 304 - static_cast<int>(r.value * 12);
      if (previous && yy - py < 96 && py - yy < 96) line(px, py, xx, yy);
      px = xx; py = yy; previous = true;
    }
  } else {
    auto r = charm::math::evaluate(expression, 0, parameter); char result[128];
    if (r.error == charm::math::Error::None) format(result, sizeof result, r.value);
    else snprintf(result, sizeof result, "%s", charm::math::error_text(r.error));
    canvas.text(result, 24, 190, r.error == charm::math::Error::None ? 0xf5c518 : 0xffb5ac, 835, 26, true);
    const char* keys = "789/(456*)123-^0.=+C";
    for (int i = 0; i < 20; ++i) {
      int x = 20 + i % 5 * 78, y = 220 + i / 5 * 48;
      canvas.rounded(x, y, 70, 40, 8, 0x1b1a18);
      char label[2] = {keys[i], 0}; canvas.text(label, x + 27, y + 28, 0xf3efe7, x + 65, 18, true);
    }
    canvas.text("Функции", 442, 250, 0xf5c518, 830, 16);
    canvas.text("sqrt(x)   abs(x)", 442, 286, 0xd8d2c7, 830, 16, true);
    canvas.text("sin(x)    cos(x)    pi", 442, 322, 0xd8d2c7, 830, 16, true);
    canvas.text("Тригонометрия — в радианах", 442, 360, 0x8e887d, 830, 12);
    canvas.text("Степень: ^ с целым показателем", 442, 388, 0x8e887d, 830, 12);
  }
  canvas.text("Enter: вычислить   Backspace: удалить   F1/F2: режим   Esc: очистить", 24, 460, 0x8e887d, 840, 11);
}
bool input(const sc::InputEvent& e) {
  if (e.type == sc::kInputKey && (e.code == 0xe1 || e.code == 0xe5)) {
    unsigned bit = e.code == 0xe1 ? 1u : 2u; if (e.value) shift |= bit; else shift &= ~bit; return false;
  }
  if (e.type == sc::kInputButton && e.code == 1 && e.value == 1) {
    if (e.y >= 14 && e.y < 52) { if (e.x >= 20 && e.x < 200) mode(false); else if (e.x >= 212 && e.x < 392) mode(true); }
    else if (!graph && e.x >= 20 && e.x < 410 && e.y >= 220 && e.y < 412) {
      int column = (e.x - 20) / 78, row = (e.y - 220) / 48;
      if ((e.x - 20) % 78 >= 70 || (e.y - 220) % 48 >= 40) return false;
      char c = "789/(456*)123-^0.=+C"[row * 5 + column];
      if (c == 'C') { expression[0] = 0; length = 0; replace = false; }
      else if (c == '=') replace = true;
      else edit(static_cast<unsigned char>(c));
    } else return false;
    return true;
  }
  if (e.type != sc::kInputKey || !e.value) return false;
  if (e.code == 0x3a) mode(false);
  else if (e.code == 0x3b) mode(true);
  else if (e.code == 0x29) { expression[0] = 0; length = 0; replace = false; }
  else if (e.code == 0x2a) { charm::ui::backspace(expression, length); replace = false; }
  else if (graph && (e.code == 0x51 || e.code == 0x52)) {
    parameter += e.code == 0x52 ? .5 : -.5;
    printf("calc: parameter %.2f\n", parameter);
  } else if (e.code == 0x28) {
    auto r = charm::math::evaluate(expression, 0, parameter);
    char text[96]; format(text, sizeof text, r.value);
    printf("calc: %s %s\n", r.error == charm::math::Error::None ? "result" : "error", r.error == charm::math::Error::None ? text : charm::math::error_text(r.error));
    replace = true;
  } else { uint32_t cp = charm::ui::key(e.code, shift != 0, false); if (!cp) return false; edit(cp); }
  return true;
}
}
extern "C" int main() {
  if (!canvas.fonts({wm_ui_font, static_cast<size_t>(wm_ui_font_end - wm_ui_font)}, {wm_mono_font, static_cast<size_t>(wm_mono_font_end - wm_mono_font)})) return 1;
  charm::gui::Window window;
  if (window.create("Калькулятор", width, height) != charm::Status::Ok) return 1;
  printf("calc: ready\n");
  int available = 0; bool dirty = true;
  for (;;) {
    if (dirty && available >= 0) {
      draw(window.pixels(static_cast<uint32_t>(available)), window.format() == sc::kDisplayBgrx);
      if (window.present(static_cast<uint32_t>(available), {0, 0, width, height}) != charm::Status::Ok) return 1;
      available = -1; dirty = false;
    }
    wm::WindowEvent e{}; auto s = window.wait(&e);
    if (s == charm::Status::Interrupted) continue;
    if (s != charm::Status::Ok || e.type == wm::kCloseRequest) return 0;
    if (e.type == wm::kFrameDone) available = static_cast<int>(e.value);
    if (e.type == wm::kInput) { sc::InputEvent event{}; while (window.next_input(&event)) if (input(event)) dirty = true; }
  }
}
