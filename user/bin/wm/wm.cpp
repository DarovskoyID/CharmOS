// Компоновщик (M10, os-gui-design.md фазы 4–6). Экран — хендл Display
// (docs/abi/display.md), ввод — своё кольцо (docs/abi/input.md), окна —
// протокол docs/abi/wm.md: клиенты подключаются через «дверь», дают два
// буфера, присылают PRESENT с прямоугольником повреждений и получают
// FRAME_DONE после вывода. Всё рисуется в задний буфер в обычной памяти; на
// экран — только повреждённое (запись во фреймбуфер — узкое место), не чаще
// 60 раз в секунду. Мышь двигает курсор и окна за заголовок, клик — фокус.
#include <abi/process.hpp>
#include <abi/wm.hpp>
#include <charm/libc.h>
#include <charm/sys.hpp>
#include <lib/font/truetype.hpp>
#include <lib/input/ring.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

namespace {

using abi::syscall::Number;
using charm::Handle;
using charm::Status;
namespace wm = abi::wm;
namespace sc = abi::syscall;

// Токены UI-кита (os-gui-design.md, «Визуальный дизайн»), 0xRRGGBB.
constexpr uint32_t kInk = 0x0b0b0c, kRaised = 0x1b1a18, kAmber = 0xf5c518, kWhite = 0xffffff;
constexpr int32_t kTitle = 52;   // Window.dc.html: высота заголовка окна
constexpr int32_t kCursor = 24;
constexpr uint64_t kFrameNs = 16'666'667;  // не чаще 60 Гц (wm.md §3)
constexpr int kMaxClients = 8, kMaxWindows = 16;
constexpr uint64_t kKeyInput = 1, kKeyDoor = 2, kKeyClient = 100;
constexpr int32_t kPanel = 40;

struct Rect {
  int32_t x, y, w, h;
  bool empty() const { return w <= 0 || h <= 0; }
  bool contains(int32_t px, int32_t py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

Rect unite(Rect a, Rect b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  int32_t x0 = a.x < b.x ? a.x : b.x, y0 = a.y < b.y ? a.y : b.y;
  int32_t x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
  int32_t y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
  return {x0, y0, x1 - x0, y1 - y0};
}

Rect intersect(Rect a, Rect b) {
  int32_t x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
  int32_t x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
  int32_t y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
  return {x0, y0, x1 - x0, y1 - y0};
}

struct Screen {
  uint32_t width, height, pitch;  // pitch — в пикселях
  bool bgrx;
  volatile uint32_t* fb;  // write-combining: только запись
  uint32_t* back;         // задний буфер, pitch = width
  uint32_t pixel(uint32_t rgb) const {
    return bgrx ? rgb : ((rgb & 0xff) << 16 | (rgb & 0xff00) | (rgb >> 16 & 0xff));
  }
  Rect all() const { return {0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height)}; }
  void fill(Rect r, uint32_t rgb, Rect clip) {
    r = intersect(intersect(r, clip), all());
    if (r.empty()) return;
    uint32_t p = pixel(rgb);
    for (int32_t y = r.y; y < r.y + r.h; ++y) {
      uint32_t* row = back + static_cast<uint64_t>(y) * width;
      for (int32_t x = r.x; x < r.x + r.w; ++x) row[x] = p;
    }
  }
  void present(Rect r) {
    r = intersect(r, all());
    if (r.empty()) return;
    for (int32_t y = r.y; y < r.y + r.h; ++y) {
      const uint32_t* src = back + static_cast<uint64_t>(y) * width + r.x;
      volatile uint32_t* dst = fb + static_cast<uint64_t>(y) * pitch + r.x;
      for (int32_t x = 0; x < r.w; ++x) dst[x] = src[x];
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
  }
};

struct Window {
  bool used;
  uint32_t id;
  int client;
  Rect frame;  // с заголовком; содержимое — frame.y + kTitle
  int32_t w, h;
  char title[64];
  const uint32_t* buf[2];
  int shown;    // показываемый буфер (-1 — ещё нет кадра)
  int pending;  // присланный PRESENT, ещё не выведенный (-1 — нет)
  Rect pending_damage;
  sc::InputRingHeader* ring;
  sc::InputEvent* events;
};

struct Client {
  bool used;
  Handle ch;
};

Screen g_s;
Window g_win[kMaxWindows];
int g_z[kMaxWindows];  // порядок снизу вверх (индексы g_win)
int g_zn = 0;
Client g_cl[kMaxClients];
uint32_t g_next_id = 1;
int g_focus = -1;
Rect g_cursor;
Rect g_damage{};
Handle g_port;
int g_drag = -1;  // окно, которое тащат за заголовок
bool g_alt = false, g_launcher = false;
bool g_control = false, g_shadows = true, g_warm = true;
bool g_files_available = false;
char g_query[64]{};
uint32_t g_query_len = 0;
int g_launcher_selected = 0;

extern "C" const uint8_t wm_ui_font[], wm_ui_font_end[], wm_mono_font[], wm_mono_font_end[];
lib::font::Face g_ui_font, g_mono_font;
lib::font::Workspace g_font_work;
struct CachedGlyph {
  bool used, mono;
  uint32_t codepoint, size;
  lib::font::Bitmap bitmap;
  uint8_t alpha[64 * 64];
};
CachedGlyph g_glyphs[256];
uint32_t g_next_glyph = 0;

CachedGlyph* glyph(uint32_t cp, bool mono, uint32_t size) {
  for (auto& c : g_glyphs) if (c.used && c.codepoint == cp && c.mono == mono && c.size == size) return &c;
  CachedGlyph& c = g_glyphs[g_next_glyph++ % 256];
  c.used = false; c.mono = mono; c.codepoint = cp; c.size = size;
  c.bitmap = {c.alpha, 64, 64, 64, 0, 0, 0};
  const auto& face = mono || g_ui_font.glyph(cp) == 0 ? g_mono_font : g_ui_font;
  if (!face.rasterize(face.glyph(cp), size, &c.bitmap, &g_font_work)) return nullptr;
  c.used = true;
  return &c;
}

void text(const char* str, int32_t x, int32_t baseline, uint32_t color, Rect clip, bool mono = false, uint32_t size = 0, int32_t tracking = 0) {
  if (size == 0) size = mono ? 14u : 16u;
  clip = intersect(clip, g_s.all());
  if (clip.empty()) return;
  uint32_t fg = g_s.pixel(color);
  while (*str != 0) {
    CachedGlyph* c = glyph(lib::font::utf8_next(&str), mono, size);
    if (c == nullptr) continue;
    auto& b = c->bitmap;
    Rect r{x + b.left, baseline + b.top, static_cast<int32_t>(b.width), static_cast<int32_t>(b.height)};
    Rect visible = intersect(r, clip);
    for (int32_t py = visible.y; py < visible.y + visible.h; ++py) {
      for (int32_t px = visible.x; px < visible.x + visible.w; ++px) {
        uint32_t a = c->alpha[static_cast<uint32_t>(py - r.y) * b.stride + static_cast<uint32_t>(px - r.x)];
        uint32_t& dst = g_s.back[static_cast<uint64_t>(py) * g_s.width + static_cast<uint32_t>(px)];
        uint32_t out = 0;
        for (unsigned shift = 0; shift < 24; shift += 8) {
          out |= ((((fg >> shift) & 255) * a + ((dst >> shift) & 255) * (255 - a) + 127) / 255) << shift;
        }
        dst = out;
      }
    }
    x += b.advance + tracking;
    if (x >= clip.x + clip.w) break;
  }
}

uint32_t blend(uint32_t a, uint32_t b, uint32_t alpha) {
  uint32_t out = 0;
  for (unsigned shift = 0; shift < 24; shift += 8)
    out |= ((((a >> shift) & 255) * alpha + ((b >> shift) & 255) * (255 - alpha) + 127) / 255) << shift;
  return out;
}

void rounded(Rect r, int32_t radius, uint32_t color, Rect clip, uint32_t opacity = 255) {
  if (radius <= 0) { g_s.fill(r, color, clip); return; }
  if (radius > r.w / 2) radius = r.w / 2;
  if (radius > r.h / 2) radius = r.h / 2;
  Rect v = intersect(intersect(r, clip), g_s.all());
  uint32_t fg = g_s.pixel(color);
  for (int32_t y = v.y; y < v.y + v.h; ++y) for (int32_t x = v.x; x < v.x + v.w; ++x) {
    auto& dst = g_s.back[static_cast<uint64_t>(y) * g_s.width + static_cast<uint32_t>(x)];
    if ((x >= r.x + radius && x < r.x + r.w - radius) ||
        (y >= r.y + radius && y < r.y + r.h - radius)) {
      dst = opacity == 255 ? fg : blend(fg, dst, opacity);
      continue;
    }
    int32_t cx = x < r.x + radius ? r.x + radius : (x >= r.x + r.w - radius ? r.x + r.w - radius : x);
    int32_t cy = y < r.y + radius ? r.y + radius : (y >= r.y + r.h - radius ? r.y + r.h - radius : y);
    unsigned coverage = 0;
    for (int32_t sy = 1; sy < 4; sy += 2) for (int32_t sx = 1; sx < 4; sx += 2) {
      int32_t dx = (x - cx) * 4 + sx, dy = (y - cy) * 4 + sy;
      coverage += dx * dx + dy * dy <= radius * radius * 16 ? 1u : 0u;
    }
    if (coverage != 0) dst = blend(fg, dst, coverage * opacity / 4);
  }
}

void shadow(Rect bounds, Rect frame, int32_t radius, uint32_t opacity, Rect clip) {
  // Measurements exposed >300 ms input-to-pixel in TCG: do not blend the
  // shadow beneath the opaque middle of a window. Keep the corner fringe.
  Rect hole{frame.x + 17, frame.y + 17, frame.w - 34, frame.h - 34};
  rounded(bounds, radius, 0, intersect(clip, {bounds.x, bounds.y, bounds.w, hole.y - bounds.y}), opacity);
  rounded(bounds, radius, 0, intersect(clip, {bounds.x, hole.y + hole.h, bounds.w, bounds.y + bounds.h - hole.y - hole.h}), opacity);
  rounded(bounds, radius, 0, intersect(clip, {bounds.x, hole.y, hole.x - bounds.x, hole.h}), opacity);
  rounded(bounds, radius, 0, intersect(clip, {hole.x + hole.w, hole.y, bounds.x + bounds.w - hole.x - hole.w, hole.h}), opacity);
}

void logo(int32_t ox, int32_t oy, int32_t size, int32_t stroke, Rect clip) {
  Rect v = intersect(intersect({ox, oy, size, size}, clip), g_s.all());
  for (int32_t y = v.y; y < v.y + v.h; ++y) for (int32_t x = v.x; x < v.x + v.w; ++x) {
    unsigned amber = 0, red = 0;
    for (int32_t sy = 1; sy < 8; sy += 2) for (int32_t sx = 1; sx < 8; sx += 2) {
      int32_t dx = ((x - ox) * 8 + sx) * 260 / size - 1040;
      int32_t dy = ((y - oy) * 8 + sy) * 260 / size - 1040;
      int32_t radius = stroke * 4;
      int64_t r2 = int64_t{dx} * dx + int64_t{dy} * dy;
      bool arc = r2 >= (624 - radius) * (624 - radius) && r2 <= (624 + radius) * (624 + radius) && !(dx > 0 && dy > -304 && dy < 304);
      bool caps = (dx - 528) * (dx - 528) + (dy - 304) * (dy - 304) <= radius * radius ||
                  (dx - 528) * (dx - 528) + (dy + 304) * (dy + 304) <= radius * radius;
      int64_t dot = int64_t{dx} * 528 - int64_t{dy} * 304;
      int64_t cross = int64_t{dy} * 528 + int64_t{dx} * 304;
      bool hand = (dot >= 0 && dot <= 371200 && cross * cross <= int64_t{radius} * radius * 371200) ||
                  r2 <= radius * radius || (dx - 528) * (dx - 528) + (dy + 304) * (dy + 304) <= radius * radius;
      if (hand) ++red; else if (arc || caps) ++amber;
    }
    auto& dst = g_s.back[static_cast<uint64_t>(y) * g_s.width + static_cast<uint32_t>(x)];
    if (amber != 0) dst = blend(g_s.pixel(kAmber), dst, amber * 255 / 16);
    if (red != 0) dst = blend(g_s.pixel(0xe0352b), dst, red * 255 / 16);
  }
}

void tile_icon(int32_t x, int32_t y, int kind, Rect clip) {
  rounded({x, y, 28, 23}, 4, kAmber, clip);
  rounded({x + 2, y + 2, 24, 19}, 2, kRaised, clip);
  if (kind == 0) {
    g_s.fill({x + 5, y + 7, 7, 9}, kAmber, clip);
    g_s.fill({x + 15, y + 7, 7, 9}, 0xe0352b, clip);
  } else {
    g_s.fill({x + 6, y + 6, 16, 2}, kAmber, clip);
    g_s.fill({x + 6, y + 11, 16, 2}, kAmber, clip);
    g_s.fill({x + 6, y + 16, 10, 2}, kAmber, clip);
  }
}

void cursor_draw(Rect clip) {
  const int outer_x[] = {0, 0, 5, 9, 14, 10, 20}, outer_y[] = {0, 20, 15, 23, 20, 13, 13};
  const int inner_x[] = {1, 1, 5, 10, 12, 8, 17}, inner_y[] = {1, 18, 13, 21, 20, 12, 12};
  auto inside = [](int x, int y, const int* xs, const int* ys) {
    bool hit = false;
    for (int i = 0, j = 6; i < 7; j = i++)
      if ((ys[i] * 2 > y) != (ys[j] * 2 > y) &&
          x < xs[i] * 2 + (y - ys[i] * 2) * (xs[j] - xs[i]) / (ys[j] - ys[i])) hit = !hit;
    return hit;
  };
  Rect v = intersect(intersect(g_cursor, clip), g_s.all());
  for (int y = v.y; y < v.y + v.h; ++y) for (int x = v.x; x < v.x + v.w; ++x) {
    int px = (x - g_cursor.x) * 2 + 1, py = (y - g_cursor.y) * 2 + 1;
    if (inside(px, py, outer_x, outer_y)) g_s.fill({x, y, 1, 1}, inside(px, py, inner_x, inner_y) ? kWhite : 0x050505, clip);
  }
}

uint32_t* g_wallpaper = nullptr;
void wallpaper_build() {
  if (g_wallpaper == nullptr) g_wallpaper = static_cast<uint32_t*>(malloc(uint64_t{g_s.width} * g_s.height * 4));
  if (g_wallpaper == nullptr) return;
  for (uint32_t y = 0; y < g_s.height; ++y) for (uint32_t x = 0; x < g_s.width; ++x) {
    int64_t dx = (int64_t{x} - g_s.width / 2) * 1024 / 550;
    int64_t dy = (int64_t{y} - g_s.height * 42 / 100) * 1024 / 350;
    int64_t distance = dx * dx + dy * dy;
    uint32_t glow = g_warm && distance < 1048576 ? static_cast<uint32_t>((1048576 - distance) * 255 / 1048576) : 0;
    g_s.back[uint64_t{y} * g_s.width + x] = g_s.pixel(blend(0x1a1613, kInk, glow));
  }
  int32_t cx = static_cast<int32_t>(g_s.width) / 2, cy = static_cast<int32_t>(g_s.height) / 2 - 42;
  logo(cx - 130, cy - 130, 260, 16, g_s.all());
  text("CHARM OS", cx - 103, cy + 162, 0xf3efe7, g_s.all(), false, 26, 5);
  text("BUILD 0.1", cx - 44, cy + 190, 0x8e887d, g_s.all(), true, 12, 2);
  memcpy(g_wallpaper, g_s.back, uint64_t{g_s.width} * g_s.height * 4);
}

void damage(Rect r);

void launcher_toggle() {
  g_launcher = !g_launcher;
  g_control = false;
  g_drag = -1;
  g_query_len = 0; g_query[0] = 0;
  g_launcher_selected = 0;
  damage(g_s.all());
  printf("wm: launcher %s\n", g_launcher ? "open" : "closed");
}

void control_toggle() {
  g_control = !g_control; g_launcher = false;
  g_drag = -1;
  damage(g_s.all()); printf("wm: control %s\n", g_control ? "open" : "closed");
}

void launch_demo() {
  char path[] = "/bin/wdemo", title[] = "Colors", color[] = "ff8a00";
  char* args[] = {path, title, color, nullptr};
  charm_spawn_opts options{};
  options.give = CHARM_GIVE_ROOT | CHARM_GIVE_CWD | CHARM_GIVE_WM | CHARM_GIVE_LOG;
  int64_t child = charm_spawn(path, args, nullptr, &options);
  if (child >= 0) (void)charm::handle_close(static_cast<Handle>(child));
  printf("wm: launch demo %s\n", child >= 0 ? "ok" : "failed");
  if (child >= 0 && g_launcher) launcher_toggle();
}

void launch_files() {
  char path[] = "/bin/files"; char* args[] = {path, nullptr};
  charm_spawn_opts options{};
  options.give = CHARM_GIVE_ROOT | CHARM_GIVE_CWD | CHARM_GIVE_WM | CHARM_GIVE_LOG;
  int64_t child = charm_spawn(path, args, nullptr, &options);
  if (child >= 0) (void)charm::handle_close(static_cast<Handle>(child));
  printf("wm: launch files %s\n", child >= 0 ? "ok" : "failed");
  if (child >= 0 && g_launcher) launcher_toggle();
}

bool files_match() { return g_files_available && (g_query_len == 0 || strstr("files", g_query) != nullptr); }

bool demo_matches() {
  return g_query_len == 0 || strstr("colors", g_query) != nullptr || strstr("demo", g_query) != nullptr;
}

bool title_matches(const char* title) {
  if (g_query_len == 0) return true;
  for (; *title; ++title) {
    const char* p = title; const char* q = g_query;
    while (*p && *q) {
      char c = *p >= 'A' && *p <= 'Z' ? static_cast<char>(*p + 32) : *p;
      if (c != *q) break;
      ++p; ++q;
    }
    if (!*q) return true;
  }
  return false;
}
int launcher_columns() {
  int x = g_s.width >= 1000 ? 120 : 32;
  int columns = (static_cast<int>(g_s.width) - 2 * x + 18) / 218;
  return columns > 0 ? columns : 1;
}
int launcher_windows() {
  int rows = (static_cast<int>(g_s.height) - 468) / 62;
  return rows > 0 ? rows * launcher_columns() : 0;
}
// One ordered set drives keyboard selection, painting and mouse hit testing.
// 0/1 are applications; 2+index identifies an existing window.
int launcher_item(int ordinal) {
  if (demo_matches() && ordinal-- == 0) return 0;
  if (files_match() && ordinal-- == 0) return 1;
  int visible = 0;
  for (int i = 0; i < g_zn && visible < launcher_windows(); ++i) {
    if (!title_matches(g_win[g_z[i]].title)) continue;
    ++visible;
    if (ordinal-- == 0) return 2 + g_z[i];
  }
  return -1;
}
int launcher_count() { int n = 0; while (launcher_item(n) >= 0) ++n; return n; }
Rect launcher_rect(int ordinal) {
  int item = launcher_item(ordinal), x = g_s.width >= 1000 ? 120 : 32;
  if (item < 0) return {};
  if (item < 2) return {x + item * 212, 240, 192, 156};
  int row = ordinal - (demo_matches() ? 1 : 0) - (files_match() ? 1 : 0);
  return {x + (row % launcher_columns()) * 218, 468 + (row / launcher_columns()) * 62, 200, 48};
}
int launcher_hit() {
  for (int i = 0; launcher_item(i) >= 0; ++i)
    if (launcher_rect(i).contains(g_cursor.x, g_cursor.y)) return i;
  return -1;
}

void desktop(Rect d) {
  g_s.fill({0, 0, static_cast<int32_t>(g_s.width), kPanel}, 0x0e0e10, d);
  g_s.fill({0, kPanel - 2, static_cast<int32_t>(g_s.width), 2}, kAmber, d);
  logo(16, 8, 22, 30, d);
  text("CHARM", 46, 25, 0xf3efe7, d, false, 13, 1);
  text("Приложения", 126, 25, 0xb7b1a6, d, false, 13);
  text("Окна", 258, 25, 0xb7b1a6, d, false, 13);
  rounded({static_cast<int32_t>(g_s.width) - 262, 9, 100, 23}, 6, g_control ? 0x332e25 : kRaised, d);
  text("Система", static_cast<int32_t>(g_s.width) - 249, 25, 0xd8d2c7, d, false, 12);
  rounded({static_cast<int32_t>(g_s.width) - 150, 9, 132, 23}, 6, kRaised, d);
  text("Alt + Space", static_cast<int32_t>(g_s.width) - 139, 25, 0xd8d2c7, d, true, 12);
  int32_t y = static_cast<int32_t>(g_s.height) - 192;
  rounded({32, y, 392, 160}, 16, 0x2a2a2e, d);
  rounded({33, y + 1, 390, 158}, 15, 0x121214, d);
  text("ЗАКРЕПЛЁННОЕ", 48, y + 25, 0x8e887d, d, false, 11, 1);
  const char* labels[] = {g_files_available ? "Файлы" : "Colors", "Приложения", "Система"};
  for (int i = 0; i < 3; ++i) {
    int32_t x = 47 + i * 122;
    rounded({x, y + 40, 114, 104}, 12, 0x332e25, d);
    rounded({x + 1, y + 41, 112, 102}, 11, kRaised, d);
    tile_icon(x + 14, y + 55, i == 0 ? 0 : 1, d);
    text(labels[i], x + 13, y + 123, 0xf3efe7, intersect({x + 4, y, 106, 160}, d), false, 13);
  }
}

void control_draw(Rect d) {
  int32_t x = static_cast<int32_t>(g_s.width) - 432, y = 56;
  int32_t h = static_cast<int32_t>(g_s.height) - 80;
  if (h > 680) h = 680;
  rounded({x, y, 400, h}, 18, 0x2a2a2e, d);
  rounded({x + 1, y + 1, 398, h - 2}, 17, 0x141416, d);
  text("Центр управления", x + 24, y + 40, 0xf3efe7, d, false, 20);
  text("Оформление рабочего стола", x + 24, y + 65, 0x8e887d, d, false, 12);
  rounded({x + 20, y + 88, 360, 76}, 12, 0x1b1a18, d);
  char display[64]; snprintf(display, sizeof display, "%u x %u", g_s.width, g_s.height);
  text("Экран", x + 36, y + 116, 0x8e887d, d, false, 12);
  text(display, x + 36, y + 144, 0xf3efe7, d, true, 17);
  char windows[64]; snprintf(windows, sizeof windows, "Открыто окон: %d", g_zn);
  text(windows, x + 222, y + 144, 0xb7b1a6, d, false, 12);
  for (int row = 0; row < 2; ++row) {
    int32_t ty = y + 192 + row * 72;
    bool enabled = row == 0 ? g_shadows : g_warm;
    text(row == 0 ? "Тени окон" : "Тёплый фон", x + 24, ty + 22, 0xf3efe7, d, false, 15);
    text(row == 0 ? "Мягкая тень под окнами" : "Свечение за логотипом", x + 24, ty + 44, 0x8e887d, d, false, 12);
    rounded({x + 328, ty + 5, 44, 24}, 12, enabled ? kAmber : 0x3a3a3f, d);
    rounded({x + 331 + (enabled ? 20 : 0), ty + 8, 18, 18}, 9, enabled ? kInk : 0xd8d2c7, d);
  }
  text("Space Grotesk / JetBrains Mono", x + 24, y + 390, 0x8e887d, d, false, 12);
  text("Alt + Space   Приложения", x + 24, y + 432, 0xd8d2c7, d, true, 12);
  text("Alt + Tab     Переключить окно", x + 24, y + 458, 0xd8d2c7, d, true, 12);
  text("Alt + F4      Закрыть окно", x + 24, y + 484, 0xd8d2c7, d, true, 12);
  text("Esc           Закрыть панель", x + 24, y + 510, 0xd8d2c7, d, true, 12);
}

void launcher_draw(Rect d) {
  if (g_launcher_selected >= launcher_count()) g_launcher_selected = 0;
  int selected_item = launcher_item(g_launcher_selected);
  int32_t x = g_s.width >= 1000 ? 120 : 32, y = 96;
  int32_t width = static_cast<int32_t>(g_s.width) - x * 2;
  g_s.fill({0, kPanel, static_cast<int32_t>(g_s.width), static_cast<int32_t>(g_s.height) - kPanel}, kInk, d);
  rounded({x, y, width, 60}, 14, 0x332e25, d);
  rounded({x + 1, y + 1, width - 2, 58}, 13, 0x121214, d);
  text(g_query_len == 0 ? "Поиск приложений" : g_query, x + 24, y + 38, g_query_len == 0 ? 0x8e887d : kWhite, d, false, 17);
  text("ESC", x + width - 54, y + 37, 0x8e887d, d, true, 12);
  rounded({x, y + 86, 68, 32}, 16, kAmber, d);
  text("Все", x + 18, y + 107, kInk, d, false, 13);
  text("Приложения и открытые окна", x + 88, y + 107, 0x8e887d, d, false, 13);
  if (files_match()) {
    int32_t tx = x + 212;
    rounded({tx, y + 144, 192, 156}, 16, selected_item == 1 ? kAmber : 0x332e25, d);
    rounded({tx + 1, y + 145, 190, 154}, 15, kRaised, d);
    tile_icon(tx + 20, y + 166, 1, d);
    text("Файлы", tx + 20, y + 248, 0xf3efe7, d, false, 15);
    text("Просмотр файлов", tx + 20, y + 274, 0x8e887d, d, false, 12);
    if (selected_item == 1) text("ENTER", tx + 122, y + 190, kAmber, d, true, 11);
  }
  if (demo_matches()) {
    rounded({x, y + 144, 192, 156}, 16, selected_item == 0 ? kAmber : 0x332e25, d);
    rounded({x + 1, y + 145, 190, 154}, 15, kRaised, d);
    tile_icon(x + 20, y + 166, 0, d);
    text("Colors", x + 20, y + 248, 0xf3efe7, d, false, 15);
    text("Демонстрация окон", x + 20, y + 274, 0x8e887d, d, false, 12);
    if (selected_item == 0) text("ENTER", x + 122, y + 190, kAmber, d, true, 11);
  } else if (launcher_count() == 0) text("Ничего не найдено", x + 20, y + 184, 0x8e887d, d);
  text("ОТКРЫТЫЕ ОКНА", x, y + 352, 0x8e887d, d, false, 11, 1);
  for (int i = 0; launcher_item(i) >= 0; ++i) {
    int item = launcher_item(i);
    if (item < 2) continue;
    Window& w = g_win[item - 2];
    Rect r = launcher_rect(i); int32_t tx = r.x, ty = r.y;
    rounded(r, 10, i == g_launcher_selected ? kAmber : 0x2a2a2e, d);
    rounded({tx + 1, ty + 1, 198, 46}, 9, 0x141416, d);
    text(w.title, tx + 16, ty + 29, 0xf3efe7, intersect({tx + 8, ty, 184, 48}, d), false, 14);
  }
}

Rect content(const Window& w) { return {w.frame.x, w.frame.y + kTitle, w.w, w.h}; }

void damage(Rect r) { if (!r.empty()) g_damage = unite(g_damage, {r.x - 16, r.y - 16, r.w + 32, r.h + 40}); }

void send_event(int client, uint32_t type, uint32_t window, uint32_t value) {
  if (client < 0 || !g_cl[client].used) return;
  wm::WindowEvent e{type, window, value};
  (void)charm::channel_write(g_cl[client].ch, &e, sizeof e, nullptr, 0);
}

void raise(int wi) {
  int at = -1;
  for (int i = 0; i < g_zn; ++i) {
    if (g_z[i] == wi) at = i;
  }
  if (at < 0) return;
  for (int i = at; i + 1 < g_zn; ++i) g_z[i] = g_z[i + 1];
  g_z[g_zn - 1] = wi;
  damage(g_win[wi].frame);
}

void set_focus(int wi) {
  if (wi == g_focus) return;
  if (g_focus >= 0 && g_win[g_focus].used) {
    send_event(g_win[g_focus].client, wm::kFocus, g_win[g_focus].id, 0);
    damage(g_win[g_focus].frame);
  }
  g_focus = wi;
  if (wi >= 0) {
    send_event(g_win[wi].client, wm::kFocus, g_win[wi].id, 1);
    damage(g_win[wi].frame);
  }
}

int window_at(int32_t x, int32_t y) {
  for (int i = g_zn - 1; i >= 0; --i) {
    if (g_win[g_z[i]].frame.contains(x, y)) return g_z[i];
  }
  return -1;
}

void destroy_window(int wi) {
  Window& w = g_win[wi];
  printf("wm: window %u destroyed\n", w.id);
  damage(w.frame);
  int k = 0;
  for (int i = 0; i < g_zn; ++i) {
    if (g_z[i] != wi) g_z[k++] = g_z[i];
  }
  g_zn = k;
  if (g_focus == wi) g_focus = -1;
  if (g_drag == wi) g_drag = -1;
  for (auto& buf : w.buf) {
    if (buf != nullptr) (void)charm::vmo_unmap(reinterpret_cast<uint64_t>(buf));
    buf = nullptr;
  }
  if (w.ring != nullptr) (void)charm::vmo_unmap(reinterpret_cast<uint64_t>(w.ring));
  w.ring = nullptr; w.used = false;
}

void drop_client(int ci) {
  for (int i = 0; i < kMaxWindows; ++i) {
    if (g_win[i].used && g_win[i].client == ci) destroy_window(i);
  }
  (void)charm::handle_close(g_cl[ci].ch);
  g_cl[ci].used = false;
}

// Событие в кольцо окна (wm.md §3) и INPUT, если кольцо было пусто.
void forward(int wi, const sc::InputEvent& e) {
  Window& w = g_win[wi];
  uint32_t head = __atomic_load_n(&w.ring->head, __ATOMIC_ACQUIRE), tail = w.ring->tail;
  if (tail - head >= w.ring->capacity) {
    ++w.ring->dropped;
    return;
  }
  w.events[tail % w.ring->capacity] = e;
  // Publish before testing emptiness again. Paired with the client's SC head
  // store + tail recheck: either it drains this event or we send a wakeup.
  if (lib::input::publish_tail(&w.ring->tail, &w.ring->head, tail + 1)) send_event(w.client, wm::kInput, w.id, 0);
}

void create_window(int ci, const wm::Create& m, const Handle* hs, uint32_t nh) {
  wm::Created reply{wm::kCreated, 0, 0, 0};
  int wi = -1;
  for (int i = 0; i < kMaxWindows; ++i) {
    if (!g_win[i].used) {
      wi = i;
      break;
    }
  }
  uint64_t bytes = uint64_t{m.width} * m.height * 4;
  bool ok = wi >= 0 && nh == 2 && m.width > 0 && m.height > 0 && m.width <= g_s.width && m.height <= g_s.height;
  uint64_t map0 = 0, map1 = 0;
  if (ok) {
    auto a = charm::vmo_map(hs[0], charm::rights::kRead);
    auto b = charm::vmo_map(hs[1], charm::rights::kRead);
    ok = a.ok() && b.ok();
    map0 = a.value;
    map1 = b.value;
  }
  // Кольцо ввода окна: создаём, отдаём клиенту дубликат (он двигает head).
  auto ring = ok ? charm::vmo_create(4096) : charm::SysResult{Status::NoMemory, 0};
  auto ring_map = ring.ok() ? charm::vmo_map(static_cast<Handle>(ring.value), charm::rights::kRead | charm::rights::kWrite)
                            : charm::SysResult{Status::NoMemory, 0};
  for (uint32_t i = 0; i < nh; ++i) (void)charm::handle_close(hs[i]);  // отображения держат VMO
  (void)bytes;
  if (!ok || !ring_map.ok()) {
    (void)charm::channel_write(g_cl[ci].ch, &reply, sizeof reply, nullptr, 0);
    return;
  }
  Window& w = g_win[wi];
  memset(&w, 0, sizeof w);
  w.used = true;
  w.id = g_next_id++;
  w.client = ci;
  w.w = static_cast<int32_t>(m.width);
  w.h = static_cast<int32_t>(m.height);
  memcpy(w.title, m.title, sizeof w.title);
  w.title[sizeof w.title - 1] = 0;
  // Каскад по слоту: каждое следующее окно правее и ниже.
  int32_t n = static_cast<int32_t>(wi);
  w.frame = {80 + n * 300, 80 + n * 150, w.w, w.h + kTitle};
  if (w.w > static_cast<int32_t>(g_s.width) / 2) {
    w.frame.x = (static_cast<int32_t>(g_s.width) - w.w) / 2;
    w.frame.y = kPanel + 56;
  } else {
    if (w.frame.x + w.frame.w > static_cast<int32_t>(g_s.width)) w.frame.x = static_cast<int32_t>(g_s.width) - w.frame.w;
    if (w.frame.y + w.frame.h > static_cast<int32_t>(g_s.height)) w.frame.y = static_cast<int32_t>(g_s.height) - w.frame.h;
    if (w.frame.y < kPanel) w.frame.y = kPanel;
  }
  w.buf[0] = reinterpret_cast<const uint32_t*>(map0);
  w.buf[1] = reinterpret_cast<const uint32_t*>(map1);
  w.shown = w.pending = -1;
  w.ring = reinterpret_cast<sc::InputRingHeader*>(ring_map.value);
  w.events = reinterpret_cast<sc::InputEvent*>(ring_map.value + sizeof(sc::InputRingHeader));
  memcpy(w.ring->magic, "INPR", 4);
  w.ring->version = 1;
  w.ring->capacity = 128;
  g_z[g_zn++] = wi;
  damage(w.frame);
  reply.window = w.id;
  reply.x = w.frame.x;
  reply.y = w.frame.y + kTitle;
  Handle give = static_cast<Handle>(ring.value);
  (void)charm::channel_write(g_cl[ci].ch, &reply, sizeof reply, &give, 1);
  set_focus(wi);  // FOCUS — после CREATED: клиент уже знает номер окна
  printf("wm: window %u \"%s\" %ux%u at %d,%d\n", w.id, w.title, m.width, m.height, w.frame.x, w.frame.y);
}

int find_window(int ci, uint32_t id) {
  for (int i = 0; i < kMaxWindows; ++i) {
    if (g_win[i].used && g_win[i].client == ci && g_win[i].id == id) return i;
  }
  return -1;
}

void client_message(int ci) {
  uint8_t msg[128];
  Handle hs[4];
  for (;;) {
    auto r = charm::channel_read(g_cl[ci].ch, msg, sizeof msg, hs, 4, 0);
    if (r.status == Status::WouldBlock) return;
    if (!r.ok()) {
      drop_client(ci);
      return;
    }
    uint32_t len = static_cast<uint32_t>(r.value >> 32), nh = static_cast<uint32_t>(r.value & 0xffffffff);
    uint32_t type = len >= 4 ? *reinterpret_cast<uint32_t*>(msg) : 0;
    if (type == wm::kCreate && len == sizeof(wm::Create)) {
      create_window(ci, *reinterpret_cast<wm::Create*>(msg), hs, nh);
      continue;
    }
    for (uint32_t i = 0; i < nh; ++i) (void)charm::handle_close(hs[i]);
    if (type == wm::kPresent && len == sizeof(wm::Present)) {
      auto& p = *reinterpret_cast<wm::Present*>(msg);
      int wi = find_window(ci, p.window);
      if (wi < 0 || p.buffer > 1 || g_win[wi].shown == static_cast<int>(p.buffer)) {
        drop_client(ci);
        return;
      }
      Window& w = g_win[wi];
      if (w.pending >= 0 && w.pending != static_cast<int>(p.buffer)) {
        send_event(ci, wm::kFrameDone, w.id, static_cast<uint32_t>(w.pending));  // слит (wm.md §3)
      }
      w.pending = static_cast<int>(p.buffer);
      Rect d = intersect({p.x, p.y, p.w, p.h}, {0, 0, w.w, w.h});
      w.pending_damage = unite(w.pending_damage, d);
      Rect c = content(w);
      damage({c.x + d.x, c.y + d.y, d.w, d.h});
    } else if (type == wm::kDestroy && len == sizeof(wm::Destroy)) {
      int wi = find_window(ci, reinterpret_cast<wm::Destroy*>(msg)->window);
      if (wi >= 0) destroy_window(wi);
    } else {
      drop_client(ci);
      return;
    }
  }
}

void door_message(Handle door) {
  wm::Helo m;
  Handle hs[1];
  for (;;) {
    auto r = charm::channel_read(door, &m, sizeof m, hs, 1, 0);
    if (!r.ok()) return;
    uint32_t len = static_cast<uint32_t>(r.value >> 32), nh = static_cast<uint32_t>(r.value & 0xffffffff);
    if (len != sizeof m || m.type != wm::kHelo || m.version != wm::kVersion || nh != 1) {
      for (uint32_t i = 0; i < nh; ++i) (void)charm::handle_close(hs[i]);
      continue;
    }
    int ci = -1;
    for (int i = 0; i < kMaxClients; ++i) {
      if (!g_cl[i].used) {
        ci = i;
        break;
      }
    }
    if (ci < 0) {
      (void)charm::handle_close(hs[0]);
      continue;
    }
    g_cl[ci] = {true, hs[0]};
    wm::Welcome w{wm::kWelcome, wm::kVersion, g_s.bgrx ? sc::kDisplayBgrx : sc::kDisplayRgbx, g_s.width, g_s.height};
    (void)charm::channel_write(hs[0], &w, sizeof w, nullptr, 0);
    (void)charm::object_bind(g_port, hs[0], kKeyClient + static_cast<uint64_t>(ci));
  }
}

void launcher_activate() {
  int item = launcher_item(g_launcher_selected);
  if (item == 0) launch_demo();
  else if (item == 1) launch_files();
  else if (item >= 2) { raise(item - 2); set_focus(item - 2); launcher_toggle(); }
}

// Ввод из ядра: курсор, перетаскивание, фокус; остальное — окну.
void input_event(const sc::InputEvent& e) {
  if (e.type == sc::kInputKey) {
    if (g_control && e.value != 0 && e.code == 0x29) { control_toggle(); return; }
    if (e.code == 0xe2 || e.code == 0xe6) { g_alt = e.value != 0; return; }
    if (g_alt && e.value == 1 && e.code == 0x2c) { launcher_toggle(); return; }
    if (g_control) {
      if (g_alt && e.value == 1 && e.code == 0x3d) control_toggle();
      return;
    }
    if (g_alt && e.value == 1 && e.code == 0x2b && g_zn > 0) {
      int wi = g_z[0]; raise(wi); set_focus(wi); return;
    }
    if (g_alt && e.value == 1 && e.code == 0x3d && g_focus >= 0) {
      send_event(g_win[g_focus].client, wm::kCloseRequest, g_win[g_focus].id, 0); return;
    }
    if (g_launcher) {
      if (e.value == 0) return;
      if (e.code == 0x29) { launcher_toggle(); return; }
      if (e.code == 0x28) { launcher_activate(); return; }
      if (e.code == 0x2b || (e.code >= 0x4f && e.code <= 0x52)) {
        int n = launcher_count();
        int step = e.code == 0x50 || e.code == 0x52 ? -1 : 1;
        if (n > 0) g_launcher_selected = (g_launcher_selected + n + step) % n;
        damage(g_s.all()); return;
      }
      if (e.code == 0x2a && g_query_len != 0) g_query[--g_query_len] = 0;
      else if (e.code >= 4 && e.code <= 29 && g_query_len + 1 < sizeof g_query) {
        g_query[g_query_len++] = static_cast<char>('a' + e.code - 4); g_query[g_query_len] = 0;
      }
      g_launcher_selected = 0;
      damage(g_s.all()); return;
    }
    if (g_control) return;
  }
  if (e.type == sc::kInputMotion) {
    Rect old = g_cursor;
    int32_t dx = e.x, dy = e.y;
    g_cursor.x += dx;
    g_cursor.y += dy;
    if (g_cursor.x < 0) g_cursor.x = 0;
    if (g_cursor.y < 0) g_cursor.y = 0;
    if (g_cursor.x > static_cast<int32_t>(g_s.width) - kCursor) g_cursor.x = static_cast<int32_t>(g_s.width) - kCursor;
    if (g_cursor.y > static_cast<int32_t>(g_s.height) - kCursor) g_cursor.y = static_cast<int32_t>(g_s.height) - kCursor;
    damage(unite(old, g_cursor));
    if (g_launcher) {
      int item = launcher_hit();
      if (item >= 0 && item != g_launcher_selected) { g_launcher_selected = item; damage(g_s.all()); }
    }
    if (g_drag >= 0) {
      Window& w = g_win[g_drag];
      damage(w.frame);
      w.frame.x += g_cursor.x - old.x;
      w.frame.y += g_cursor.y - old.y;
      if (w.frame.y < kPanel) w.frame.y = kPanel;
      damage(w.frame);
    }
    if (g_drag < 0 && !g_launcher && !g_control) {
      int wi = window_at(g_cursor.x, g_cursor.y);
      if (wi >= 0 && content(g_win[wi]).contains(g_cursor.x, g_cursor.y)) {
        auto fe = e; Rect c = content(g_win[wi]);
        fe.x = g_cursor.x - c.x; fe.y = g_cursor.y - c.y; forward(wi, fe);
      }
    }
    return;
  }
  if (e.type == sc::kInputButton && e.value == 1 && e.code == 1 && g_cursor.y < kPanel) {
    if (g_cursor.x >= static_cast<int32_t>(g_s.width) - 262 && g_cursor.x < static_cast<int32_t>(g_s.width) - 162) control_toggle();
    else if (g_cursor.x >= 100 && g_cursor.x < 250) launcher_toggle();
    else if (g_cursor.x >= 250 && g_cursor.x < 320 && g_zn > 0) { int wi = g_z[0]; raise(wi); set_focus(wi); }
    return;
  }
  if (g_control) {
    if (e.type == sc::kInputButton && e.code == 1 && e.value == 1) {
      int32_t x = static_cast<int32_t>(g_s.width) - 432;
      if (Rect{x + 328, 248, 44, 24}.contains(g_cursor.x, g_cursor.y)) {
        g_shadows = !g_shadows; damage(g_s.all()); printf("wm: shadows %s\n", g_shadows ? "on" : "off");
      } else if (Rect{x + 328, 320, 44, 24}.contains(g_cursor.x, g_cursor.y)) {
        g_warm = !g_warm; wallpaper_build(); damage(g_s.all()); printf("wm: warm %s\n", g_warm ? "on" : "off");
      } else if (!Rect{x, 56, 400, static_cast<int32_t>(g_s.height) - 80}.contains(g_cursor.x, g_cursor.y)) control_toggle();
    }
    return;
  }
  if (g_launcher) {
    if (e.type == sc::kInputButton && e.code == 1 && e.value == 1) {
      int item = launcher_hit();
      if (item >= 0) { g_launcher_selected = item; launcher_activate(); }
    }
    return;
  }
  if (e.type == sc::kInputButton && e.value == 1 && e.code == 1 &&
      Rect{32, static_cast<int32_t>(g_s.height) - 192, 392, 160}.contains(g_cursor.x, g_cursor.y) &&
      window_at(g_cursor.x, g_cursor.y) < 0) {
    int32_t y = static_cast<int32_t>(g_s.height) - 152;
    if (Rect{47, y, 114, 104}.contains(g_cursor.x, g_cursor.y)) { if (g_files_available) launch_files(); else launch_demo(); }
    else if (Rect{169, y, 114, 104}.contains(g_cursor.x, g_cursor.y)) launcher_toggle();
    else if (Rect{291, y, 114, 104}.contains(g_cursor.x, g_cursor.y)) control_toggle();
    return;
  }
  if (e.type == sc::kInputButton) {
    if (e.value != 0) {
      int wi = window_at(g_cursor.x, g_cursor.y);
      if (wi >= 0) {
        raise(wi);
        set_focus(wi);
        if (g_cursor.y < g_win[wi].frame.y + kTitle) {
          if (e.code == 1 && Rect{g_win[wi].frame.x + 16, g_win[wi].frame.y + 16, 20, 20}.contains(g_cursor.x, g_cursor.y)) {
            send_event(g_win[wi].client, wm::kCloseRequest, g_win[wi].id, 0); return;
          }
          if (e.code == 1) g_drag = wi;  // за заголовок — тащим (событие окну не идёт)
          return;
        }
      }
    } else if (g_drag >= 0) {
      printf("wm: window %u moved to %d,%d\n", g_win[g_drag].id, g_win[g_drag].frame.x, g_win[g_drag].frame.y);
      g_drag = -1;
      return;
    }
  }
  int target = e.type == sc::kInputKey ? g_focus : window_at(g_cursor.x, g_cursor.y);
  if (target < 0 || (e.type != sc::kInputKey && !content(g_win[target]).contains(g_cursor.x, g_cursor.y))) return;
  sc::InputEvent fe = e;
  Rect c = content(g_win[target]);
  fe.x = g_cursor.x - c.x;
  fe.y = g_cursor.y - c.y;
  if (e.type == sc::kInputKey) {
    fe.x = fe.y = 0;
    if (e.value == 1) printf("wm: key %#x -> window %u\n", e.code, g_win[g_focus].id);
  }
  forward(target, fe);
}

// Кадр: повреждённое заново в заднем буфере (фон, окна снизу вверх, курсор), на экран.
void compose() {
  Rect d = intersect(g_damage, g_s.all());
  g_damage = {};
  if (d.empty()) return;
  if (g_wallpaper != nullptr) {
    for (int32_t y = d.y; y < d.y + d.h; ++y)
      memcpy(g_s.back + uint64_t{static_cast<uint32_t>(y)} * g_s.width + d.x,
             g_wallpaper + uint64_t{static_cast<uint32_t>(y)} * g_s.width + d.x, static_cast<size_t>(d.w) * 4);
  } else g_s.fill(d, kInk, d);
  desktop(d);
  for (int i = 0; i < g_zn; ++i) {
    Window& w = g_win[g_z[i]];
    Rect bar{w.frame.x, w.frame.y, w.frame.w, kTitle};
    if (g_shadows) {
      shadow({bar.x - 12, bar.y - 4, w.frame.w + 24, w.frame.h + 24}, w.frame, 28, 24, d);
      shadow({bar.x - 6, bar.y, w.frame.w + 12, w.frame.h + 12}, w.frame, 22, 40, d);
    }
    rounded({bar.x - 1, bar.y - 1, w.frame.w + 2, w.frame.h + 2}, 18, g_z[i] == g_focus ? 0x5b5140 : 0x2a2a2e, d);
    rounded(w.frame, 17, 0x17171a, d);
    g_s.fill({bar.x, bar.y + kTitle - 1, bar.w, 1}, 0x2a2a2e, d);
    rounded({bar.x + 16, bar.y + 20, 12, 12}, 3, 0xe0352b, d);
    text(w.title, bar.x + 44, bar.y + 31, g_z[i] == g_focus ? 0xf3efe7 : 0x8e887d,
         intersect({bar.x + 40, bar.y, bar.w - 52, bar.h}, d), false, 14);
    Rect c = intersect(intersect(content(w), d), g_s.all());
    if (w.pending >= 0) {
      w.shown = w.pending;
      w.pending = -1;
      w.pending_damage = {};
    }
    if (c.empty()) continue;
    if (w.shown < 0) {
      g_s.fill(c, kRaised, d);
      continue;
    }
    const uint32_t* src = w.buf[w.shown];
    Rect cc = content(w);
    for (int32_t y = c.y; y < c.y + c.h; ++y) {
      const uint32_t* s = src + static_cast<uint64_t>(y - cc.y) * static_cast<uint64_t>(w.w) + (c.x - cc.x);
      uint32_t* t = g_s.back + static_cast<uint64_t>(y) * g_s.width + c.x;
      if (y < w.frame.y + w.frame.h - 17) memcpy(t, s, static_cast<size_t>(c.w) * 4);
      else for (int32_t x = c.x; x < c.x + c.w; ++x) {
        if (x >= w.frame.x + 17 && x < w.frame.x + w.frame.w - 17) { t[x - c.x] = s[x - c.x]; continue; }
        int32_t cx = x < w.frame.x + 17 ? w.frame.x + 17 :
                     (x >= w.frame.x + w.frame.w - 17 ? w.frame.x + w.frame.w - 17 : x);
        int32_t cy = w.frame.y + w.frame.h - 17;
        unsigned samples = 0;
        for (int sy = 1; sy < 4; sy += 2) for (int sx = 1; sx < 4; sx += 2) {
          int32_t dx = (x - cx) * 4 + sx, dy = (y - cy) * 4 + sy;
          samples += dx * dx + dy * dy <= 17 * 17 * 16 ? 1u : 0u;
        }
        t[x - c.x] = blend(s[x - c.x], t[x - c.x], samples * 255 / 4);
      }
    }
  }
  if (g_launcher) launcher_draw(d);
  if (g_control) control_draw(d);
  cursor_draw(d);
  g_s.present(d);
}

}  // namespace

extern "C" int main(int, char**) {
  Handle display = charm_display(), input = charm_input();
  Handle door = charm_extra_handle(0);  // wm.md §1: конец двери — хендл без роли
  if (display == 0) {
    printf("wm: no display handle\n");
    return 1;
  }
  sc::DisplayInfo info{};
  if (charm::syscall2(Number::DisplayInfo, display, reinterpret_cast<uint64_t>(&info)).status != Status::Ok) {
    printf("wm: display_info failed (unsupported pixel format?)\n");
    return 1;
  }
  auto vmo = charm::syscall1(Number::DisplayMap, display);
  auto fb = vmo.ok() ? charm::vmo_map(static_cast<Handle>(vmo.value), charm::rights::kRead | charm::rights::kWrite)
                     : charm::SysResult{Status::NoMemory, 0};
  auto back_vmo = charm::vmo_create((uint64_t{info.width} * info.height * 4 + 4095) / 4096 * 4096);
  auto back = back_vmo.ok() ? charm::vmo_map(static_cast<Handle>(back_vmo.value), charm::rights::kRead | charm::rights::kWrite)
                            : charm::SysResult{Status::NoMemory, 0};
  if (!fb.ok() || !back.ok()) {
    printf("wm: cannot map framebuffer\n");
    return 1;
  }
  g_s = {info.width, info.height, info.pitch / 4, info.format == sc::kDisplayBgrx,
         reinterpret_cast<volatile uint32_t*>(fb.value), reinterpret_cast<uint32_t*>(back.value)};
  bool fonts_ok = g_ui_font.open({wm_ui_font, static_cast<size_t>(wm_ui_font_end - wm_ui_font)}) &&
                  g_mono_font.open({wm_mono_font, static_cast<size_t>(wm_mono_font_end - wm_mono_font)});
  if (!fonts_ok) { printf("wm: invalid embedded font\n"); return 1; }
  struct stat files_stat{};
  g_files_available = stat("/bin/files", &files_stat) == 0 && S_ISREG(files_stat.st_mode);
  wallpaper_build();
  printf("wm: TrueType fonts ready (Space Grotesk, JetBrains Mono)\n");
  printf("wm: display %ux%u, pitch %u, %s\n", info.width, info.height, info.pitch, g_s.bgrx ? "BGRX" : "RGBX");
  g_cursor = {static_cast<int32_t>(info.width / 2), static_cast<int32_t>(info.height / 2), kCursor, kCursor};
  uint64_t t0 = charm::now_ns();
  damage(g_s.all());
  compose();
  printf("wm: full frame %llu us\n", static_cast<unsigned long long>((charm::now_ns() - t0) / 1000));

  auto port = charm::port_create();
  if (!port.ok()) return 1;
  g_port = static_cast<Handle>(port.value);
  // Кольцо ввода компоновщика (input.md §2).
  sc::InputRingHeader* ring = nullptr;
  sc::InputEvent* events = nullptr;
  if (input != 0) {
    auto rv = charm::vmo_create(4096);
    auto rm = rv.ok() ? charm::vmo_map(static_cast<Handle>(rv.value), charm::rights::kRead | charm::rights::kWrite)
                      : charm::SysResult{Status::NoMemory, 0};
    if (rm.ok()) {
      ring = reinterpret_cast<sc::InputRingHeader*>(rm.value);
      events = reinterpret_cast<sc::InputEvent*>(rm.value + sizeof *ring);
      memcpy(ring->magic, "INPR", 4);
      ring->version = 1;
      ring->capacity = 128;
      if (charm::syscall2(Number::InputAttach, input, static_cast<Handle>(rv.value)).status != Status::Ok) ring = nullptr;
    }
  }
  if (door != 0) (void)charm::object_bind(g_port, door, kKeyDoor);
  printf("wm: ready (input %s, door %s)\n", ring != nullptr ? "yes" : "no", door != 0 ? "yes" : "no");

  uint64_t last_frame = 0;
  for (;;) {
    if (ring != nullptr) (void)charm::object_bind(g_port, input, kKeyInput);
    uint64_t deadline = charm::kForever;
    if (!g_damage.empty()) deadline = last_frame + kFrameNs;
    charm::PortPacket pk{};
    Status s = charm::port_wait(g_port, &pk, deadline);
    if (s == Status::Ok) {
      if (pk.key == kKeyInput && ring != nullptr) {
        uint32_t tail = __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE);
        for (uint32_t head = ring->head; head != tail; ++head) input_event(events[head % ring->capacity]);
        __atomic_store_n(&ring->head, tail, __ATOMIC_RELEASE);
      } else if (pk.key == kKeyDoor) {
        door_message(door);
        (void)charm::object_bind(g_port, door, kKeyDoor);
      } else if (pk.key >= kKeyClient && pk.key < kKeyClient + kMaxClients) {
        int ci = static_cast<int>(pk.key - kKeyClient);
        if (g_cl[ci].used) {
          client_message(ci);
          if (g_cl[ci].used) (void)charm::object_bind(g_port, g_cl[ci].ch, pk.key);
        }
      }
    }
    uint64_t now = charm::now_ns();
    if (!g_damage.empty() && now >= last_frame + kFrameNs) {
      // Буферы, которые станут показанными в этом кадре: прежние — клиенту после вывода.
      int done_win[kMaxWindows];
      int done_buf[kMaxWindows];
      int nd = 0;
      for (int i = 0; i < kMaxWindows; ++i) {
        if (g_win[i].used && g_win[i].pending >= 0) {
          done_win[nd] = i;
          done_buf[nd++] = g_win[i].shown >= 0 ? g_win[i].shown : 1 - g_win[i].pending;
        }
      }
      compose();
      last_frame = now;
      for (int k = 0; k < nd; ++k) {
        Window& w = g_win[done_win[k]];
        send_event(w.client, wm::kFrameDone, w.id, static_cast<uint32_t>(done_buf[k]));
      }
    }
  }
}
