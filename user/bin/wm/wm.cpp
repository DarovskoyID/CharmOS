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
#include <charm/expression.hpp>
#include <charm/text_input.hpp>
#include <charm/units.hpp>
#include <charm/preferences.hpp>
#include <lib/crypto/sha512.hpp>
#include <fcntl.h>
#include <pthread.h>
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
  bool minimized;
  uint32_t id;
  uint32_t generation;
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

constexpr unsigned kOutboundMessages=64;
struct OutboundMessage {
  uint32_t size;
  Handle capability;
  alignas(4) uint8_t bytes[sizeof(wm::WelcomeV3)];
};
struct Client {
  bool used;
  bool failed;
  Handle ch;
  uint32_t version;
  unsigned output_head,output_count;
  OutboundMessage output[kOutboundMessages];
};

Screen g_s;
Handle g_wm_door = 0;  // WM capability passed to the browser on demand
Handle g_init_ctl = 0;  // канал управления init: запуск службы браузера
bool g_browser_focus_pending = false;
// Индекс окна браузера запоминается при создании: после загрузки страницы
// заголовок меняется с "Chromium" на заголовок сайта, и поиск по имени
// перестаёт его находить. -1 — окна нет.
int g_browser_window = -1;
// 0 — простой, 1 — content_shell запускается в фоновом потоке, 2 — запуск
// не удался. Пишется из потока, читается циклом событий (атомарно).
volatile int g_browser_launch_state = 0;
uint64_t g_browser_focus_deadline = 0;
uint64_t g_browser_request_ns = 0;
Window g_win[kMaxWindows];
int g_z[kMaxWindows];  // порядок снизу вверх (индексы g_win)
int g_zn = 0;
Client g_cl[kMaxClients];
uint32_t g_next_id = 1;
int g_focus = -1;
int g_capture = -1;
Rect g_cursor;
Rect g_damage{};
Rect g_regions[16]{};
unsigned g_region_count = 0;
Rect g_hot{};
bool hot(Rect r) { return r.x == g_hot.x && r.y == g_hot.y && r.w == g_hot.w && r.h == g_hot.h; }
Handle g_port;
int g_drag = -1;  // окно, которое тащат за заголовок
unsigned g_alt = 0;
bool g_launcher = false;
bool g_quick = false, g_russian = false;
char g_quick_notice[128]{};
unsigned g_shift = 0;
bool g_control = false, g_shadows = true, g_warm = true;
bool g_files_available = false;
bool g_calc_available = false;
bool g_edit_available = false;
bool g_browser_available = false;
char g_query[64]{};
uint32_t g_query_len = 0;
int g_launcher_selected = 0;
uint32_t g_preference_generation = 0;
bool g_preferences_saved = true;

void release_capture();

void load_preferences() {
  charm::ui::Preferences chosen{};
  for (int slot=0;slot<2;++slot) {
    char path[32]; snprintf(path,sizeof path,"/etc/charm-desktop.%d",slot);
    int fd=open(path,O_RDONLY); if(fd<0) continue;
    charm::ui::Preferences p{}; char extra;
    bool ok=read(fd,&p,sizeof p)==static_cast<ssize_t>(sizeof p) && read(fd,&extra,1)==0;
    close(fd);
    if(ok && charm::ui::valid(p) && p.generation>chosen.generation) chosen=p;
  }
  if(chosen.generation) {
    g_preference_generation=chosen.generation;
    g_shadows=(chosen.flags&1)!=0; g_warm=(chosen.flags&2)!=0; g_russian=(chosen.flags&4)!=0;
    printf("wm: preferences loaded %u flags %u\n",chosen.generation,chosen.flags);
  }
}
void save_preferences() {
  g_preferences_saved=false;
  if(g_preference_generation==0xffffffffu) return;
  uint32_t generation=g_preference_generation+1;
  auto p=charm::ui::preferences(generation,(g_shadows?1u:0u)|(g_warm?2u:0u)|(g_russian?4u:0u));
  char path[32]; snprintf(path,sizeof path,"/etc/charm-desktop.%u",generation&1u);
  int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0600); if(fd<0) return;
  bool ok=write(fd,&p,sizeof p)==static_cast<ssize_t>(sizeof p) && fsync(fd)==0; close(fd);
  if(ok) { g_preferences_saved=true; g_preference_generation=generation; printf("wm: preferences saved %u flags %u\n",generation,p.flags); }
}

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
      // Main.dc.html: M196 92 a78 78 0 1 0 0 76. The circle centre is
      // (196 - sqrt(78^2 - 38^2), 130), not (130,130). Clip the opening
      // by rays through its endpoints, then add round caps like the SVG.
      int32_t arc_x = dx + 17;
      int64_t arc_r2 = int64_t{arc_x} * arc_x + int64_t{dy} * dy;
      bool opening = arc_x > 0 && int64_t{dy < 0 ? -dy : dy} * 545 < int64_t{arc_x} * 304;
      bool arc = arc_r2 >= (624 - radius) * (624 - radius) && arc_r2 <= (624 + radius) * (624 + radius) && !opening;
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
  if (kind == 1) {  // Folder, matching the file action rather than Colors.
    rounded({x, y + 2, 12, 7}, 2, kAmber, clip);
    rounded({x, y + 6, 28, 18}, 3, kAmber, clip);
    rounded({x + 2, y + 8, 24, 14}, 2, kRaised, clip);
    return;
  }
  if (kind == 2) {
    for (int row = 0; row < 2; ++row) for (int column = 0; column < 2; ++column) {
      rounded({x + column * 15, y + row * 13, 11, 10}, 2, kAmber, clip);
      rounded({x + column * 15 + 2, y + row * 13 + 2, 7, 6}, 1, kRaised, clip);
    }
    return;
  }
  if (kind == 3) {
    for (int row = 0; row < 3; ++row) {
      int cy = y + 4 + row * 8, cx = x + (row == 1 ? 19 : 8);
      g_s.fill({x, cy, 28, 2}, kAmber, clip);
      rounded({cx - 4, cy - 3, 8, 8}, 3, kAmber, clip);
      rounded({cx - 2, cy - 1, 4, 4}, 1, kRaised, clip);
    }
    return;
  }
  if (kind == 4) {
    // Browser window: toolbar, address line and page text.
    rounded({x, y, 28, 23}, 4, kAmber, clip);
    rounded({x + 2, y + 2, 24, 19}, 2, kRaised, clip);
    g_s.fill({x + 3, y + 4, 22, 4}, kAmber, clip);
    g_s.fill({x + 4, y + 10, 20, 2}, kWhite, clip);
    g_s.fill({x + 4, y + 14, 15, 2}, kAmber, clip);
    g_s.fill({x + 4, y + 18, 18, 2}, kAmber, clip);
    return;
  }
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

void launcher_toggle(bool quick = false) {
  if(!g_launcher) release_capture();
  g_launcher = !g_launcher;
  g_quick = quick;
  g_quick_notice[0] = 0;
  g_control = false;
  g_drag = -1;
  g_query_len = 0; g_query[0] = 0;
  g_launcher_selected = 0;
  damage(g_s.all());
  printf("wm: launcher %s\n", g_launcher ? "open" : "closed");
}

void control_toggle() {
  if(!g_control) release_capture();
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

void launch_calc() {
  char path[] = "/bin/calc"; char* args[] = {path, nullptr};
  charm_spawn_opts options{};
  options.give = CHARM_GIVE_ROOT | CHARM_GIVE_CWD | CHARM_GIVE_WM | CHARM_GIVE_LOG;
  int64_t child = charm_spawn(path, args, nullptr, &options);
  if (child >= 0) (void)charm::handle_close(static_cast<Handle>(child));
  printf("wm: launch calc %s\n", child >= 0 ? "ok" : "failed");
  if (child >= 0 && g_launcher) launcher_toggle();
}
bool files_match() { return g_files_available && (g_query_len == 0 || strstr("files", g_query) != nullptr || strstr("файлы", g_query) != nullptr); }
bool calc_match() { return g_calc_available && (g_query_len == 0 || strstr("calc", g_query) != nullptr || strstr("калькулятор", g_query) != nullptr); }
bool edit_match() { return g_edit_available && (g_query_len==0 || strstr("editor",g_query)!=nullptr || strstr("редактор",g_query)!=nullptr); }
void launch_edit() {
  char path[]="/bin/edit"; char* args[]={path,nullptr}; charm_spawn_opts options{};
  options.give=CHARM_GIVE_ROOT|CHARM_GIVE_CWD|CHARM_GIVE_WM|CHARM_GIVE_LOG;
  int64_t child=charm_spawn(path,args,nullptr,&options);
  if(child>=0) charm::handle_close(static_cast<Handle>(child));
  printf("wm: launch edit %s\n",child>=0?"ok":"failed");
  if(child>=0 && g_launcher) launcher_toggle();
}

void raise(int wi);
void set_focus(int wi);

bool browser_match() {
  return g_browser_available &&
         (g_query_len == 0 || strstr("browser", g_query) != nullptr ||
          strstr("chromium", g_query) != nullptr ||
          strstr("браузер", g_query) != nullptr ||
          strstr("интернет", g_query) != nullptr);
}

// The browser itself is a service: init owns the 200 MiB content_shell
// process, restarts it after a crash, and never blocks the event loop here.
// The card only asks init to start it (see launch_browser).

// A window is the browser if it was recorded at creation time (its title
// becomes the page title after the first load) or still matches the service
// names. Returns -1 when the browser window is not present.
int find_browser_window() {
  if (g_browser_window >= 0 && g_browser_window < kMaxWindows &&
      g_win[g_browser_window].used) {
    return g_browser_window;
  }
  for (int i = 0; i < g_zn; ++i) {
    const char* title = g_win[g_z[i]].title;
    if (strstr(title, "Chromium") != nullptr ||
        strstr(title, "CharmOS") != nullptr) {
      g_browser_window = g_z[i];
      return g_browser_window;
    }
  }
  return -1;
}

void init_command(const char* text) {
  if (g_init_ctl == 0) return;
  Status st = charm::channel_write(g_init_ctl, text,
                                   static_cast<uint64_t>(strlen(text)), nullptr, 0);
  if (st != Status::Ok) {
    printf("wm: init command '%s' failed %d\n", text, static_cast<int>(st));
  }
}

// Ask init for a fresh browser process. A stopped service starts directly; a
// running-but-windowless process (a hang or a crash before the exit packet is
// reaped) is killed first, otherwise "start" would answer "running" and the
// card would appear to do nothing.
void request_browser_start(bool kill_first) {
  if (kill_first) init_command("kill browser");
  init_command("start browser");
  g_browser_request_ns = charm::now_ns();
  printf("wm: browser start requested (%s)\n", kill_first ? "restart" : "start");
}

void launch_browser() {
  int wi = find_browser_window();
  if (wi >= 0) {
    raise(wi); set_focus(wi);
    if (g_launcher) launcher_toggle();
    printf("wm: focus browser\n");
    return;
  }
  int state = __atomic_load_n(&g_browser_launch_state, __ATOMIC_ACQUIRE);
  if (state == 1) {  // уже загружается
    snprintf(g_quick_notice, sizeof g_quick_notice, "Браузер запускается…");
    if (g_launcher) launcher_toggle();
    damage(g_s.all());
    return;
  }
  // TODO(hang-protection): add a WM/init watchdog that bounds service startup
  // and window-message stalls, then reports or restarts the offender instead
  // of freezing the whole desktop. g_browser_focus_deadline below is the
  // first step: a launch that produces no window stops the retry timer.
  __atomic_store_n(&g_browser_launch_state, 1, __ATOMIC_RELEASE);
  g_browser_focus_pending = true;
  g_browser_focus_deadline = charm::now_ns() + 180'000'000'000ull;
  snprintf(g_quick_notice, sizeof g_quick_notice, "Браузер запускается…");
  if (g_launcher) launcher_toggle();
  damage(g_s.all());
  request_browser_start(false);
  if (g_init_ctl == 0) {
    printf("wm: no init control channel\n");
  }
}

// Bring the browser window forward as soon as it appears, and stop retrying
// if the launch produced no window within the deadline.
void poll_browser_focus() {
  if (__atomic_load_n(&g_browser_launch_state, __ATOMIC_ACQUIRE) == 2) {
    __atomic_store_n(&g_browser_launch_state, 0, __ATOMIC_RELEASE);
    g_browser_focus_pending = false;
    snprintf(g_quick_notice, sizeof g_quick_notice, "Браузер не запустился");
    damage(g_s.all());
    printf("wm: browser launch failed\n");
    return;
  }
  if (!g_browser_focus_pending) return;
  if (g_browser_focus_deadline != 0 && charm::now_ns() > g_browser_focus_deadline) {
    g_browser_focus_pending = false;
    __atomic_store_n(&g_browser_launch_state, 0, __ATOMIC_RELEASE);
    snprintf(g_quick_notice, sizeof g_quick_notice, "Браузер не открылся");
    damage(g_s.all());
    printf("wm: browser window timeout\n");
    return;
  }
  int wi = find_browser_window();
  if (wi < 0) {
    // The first request can race with init reaping the previous process
    // ("running" for a process that just died). Keep asking every 3 s; init
    // answers "running" while the service is alive, which is harmless.
    if (g_browser_request_ns != 0 &&
        charm::now_ns() - g_browser_request_ns > 3'000'000'000ull) {
      init_command("start browser");
      g_browser_request_ns = charm::now_ns();
    }
    return;
  }
  g_browser_focus_pending = false;
  g_browser_focus_deadline = 0;
  g_browser_request_ns = 0;
  __atomic_store_n(&g_browser_launch_state, 0, __ATOMIC_RELEASE);
  raise(wi); set_focus(wi);
  printf("wm: focused pending browser\n");
}

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
int app_columns() {
  int x=g_s.width>=1000?120:32;
  int count=(static_cast<int>(g_s.width)-2*x+20)/212;
  return count>0?count:1;
}
Rect app_rect(int slot) {
  int x=g_s.width>=1000?120:32;
  return {x+(slot%app_columns())*212,240+(slot/app_columns())*176,192,156};
}
int window_cards_y() {
  int slots=g_browser_available?5:g_edit_available?4:g_calc_available?3:g_files_available?2:1;
  return 292+((slots+app_columns()-1)/app_columns())*176;
}
int launcher_windows() {
  int rows = (static_cast<int>(g_s.height) - window_cards_y()) / 62;
  return rows > 0 ? rows * launcher_columns() : 0;
}
// One ordered set drives keyboard selection, painting and mouse hit testing.
// 0/1 are applications; 2+index identifies an existing window.
int launcher_item(int ordinal) {
  if (demo_matches() && ordinal-- == 0) return 0;
  if (files_match() && ordinal-- == 0) return 1;
  if (calc_match() && ordinal-- == 0) return 100;
  if (edit_match() && ordinal-- == 0) return 101;
  if (browser_match() && ordinal-- == 0) return 102;
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
  if (g_quick) {
    int first = g_launcher_selected > 2 ? g_launcher_selected - 2 : 0;
    if (ordinal < first || ordinal >= first + 3) return {};
    return {static_cast<int32_t>(g_s.width) - 408, static_cast<int32_t>(g_s.height) - 204 + (ordinal - first) * 40, 360, 36};
  }
  int item = launcher_item(ordinal), x = g_s.width >= 1000 ? 120 : 32;
  if (item < 0) return {};
  if (item < 2) return app_rect(item);
  if (item >= 100) return app_rect(item==100 ? 2 : item==101 ? 3 : 4);
  int row = ordinal - (demo_matches() ? 1 : 0) - (files_match() ? 1 : 0) - (calc_match() ? 1 : 0) - (edit_match()?1:0) - (browser_match()?1:0);
  return {x + (row % launcher_columns()) * 218, window_cards_y() + (row / launcher_columns()) * 62, 200, 48};
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
  if (hot({100, 4, 150, 30})) rounded({100, 4, 150, 30}, 7, 0x29251b, d);
  if (hot({250, 4, 70, 30})) rounded({250, 4, 70, 30}, 7, 0x29251b, d);
  text("Приложения", 126, 25, 0xb7b1a6, d, false, 13);
  text("Окна", 258, 25, 0xb7b1a6, d, false, 13);
  rounded({static_cast<int32_t>(g_s.width) - 262, 9, 100, 23}, 6, g_control || hot({static_cast<int32_t>(g_s.width) - 262, 9, 100, 23}) ? 0x332e25 : kRaised, d);
  text("Система", static_cast<int32_t>(g_s.width) - 249, 25, 0xd8d2c7, d, false, 12);
  rounded({static_cast<int32_t>(g_s.width) - 150, 9, 132, 23}, 6, hot({static_cast<int32_t>(g_s.width) - 150, 9, 132, 23}) ? 0x332e25 : kRaised, d);
  text("Alt + Space", static_cast<int32_t>(g_s.width) - 139, 25, 0xd8d2c7, d, true, 12);
  int32_t y = static_cast<int32_t>(g_s.height) - 192;
  rounded({32, y, 392, 160}, 16, 0x2a2a2e, d);
  rounded({33, y + 1, 390, 158}, 15, 0x121214, d);
  text("ЗАКРЕПЛЁННОЕ", 48, y + 25, 0x8e887d, d, false, 11, 1);
  const char* labels[] = {g_files_available ? "Файлы" : "Colors", "Приложения", "Система"};
  for (int i = 0; i < 3; ++i) {
    int32_t x = 47 + i * 122;
    bool over = hot({x, y + 40, 114, 104});
    rounded({x, y + 40, 114, 104}, 12, over ? kAmber : 0x332e25, d);
    rounded({x + 1, y + 41, 112, 102}, 11, over ? 0x29251b : kRaised, d);
    tile_icon(x + 14, y + 55, i == 0 ? (g_files_available ? 1 : 0) : i + 1, d);
    text(labels[i], x + 13, y + 123, 0xf3efe7, intersect({x + 4, y, 106, 160}, d), false, 13);
  }
  int32_t qx = static_cast<int32_t>(g_s.width) - 424;
  int32_t qy = static_cast<int32_t>(g_s.height) - 192;
  rounded({qx, qy, 392, 160}, 16, 0x2a2a2e, d);
  rounded({qx + 1, qy + 1, 390, 158}, 15, 0x121214, d);
  text("УНИВЕРСАЛЬНЫЙ ДОСТУП", qx + 16, qy + 26, 0x8e887d, d, false, 10);
  text("ALT+SPACE", qx + 288, qy + 26, 0x8e887d, d, true, 10);
  rounded({qx + 14, qy + 42, 364, 44}, 9, 0x332e25, d);
  rounded({qx + 15, qy + 43, 362, 42}, 8, kInk, d);
  text("Поиск, =выражение, /команда", qx + 26, qy + 70, 0xb7b1a6, d, false, 13);
  text("= Вычислить    / Команды", qx + 18, qy + 113, 0xf3efe7, d, false, 12);
  text("Программы, файлы и открытые окна", qx + 18, qy + 139, 0x8e887d, d, false, 11);
}

void quick_draw(Rect d) {
  if(g_launcher_selected>=launcher_count()) g_launcher_selected=0;
  int32_t x = static_cast<int32_t>(g_s.width) - 424, y = static_cast<int32_t>(g_s.height) - 312;
  Rect clip = intersect({x, y, 392, 280}, d);
  rounded({x, y, 392, 280}, 16, 0x5b5140, d);
  rounded({x + 1, y + 1, 390, 278}, 15, 0x121214, d);
  text("УНИВЕРСАЛЬНЫЙ ДОСТУП", x + 16, y + 26, 0x8e887d, clip, false, 10);
  text(g_russian ? "RU" : "EN", x + 344, y + 26, kAmber, clip, true, 11);
  rounded({x + 14, y + 42, 364, 48}, 9, 0x332e25, clip);
  text(g_query_len ? g_query : "Введите запрос…", x + 26, y + 73, kWhite, intersect({x + 20, y + 42, 350, 48}, clip), false, 15);
  if (g_query[0] == '=') {
    auto result = charm::math::evaluate(g_query + 1);
    char value[96];
    if (result.error != charm::math::Error::None) snprintf(value, sizeof value, "%s", charm::math::error_text(result.error));
    else if (result.value > 1e12 || result.value < -1e12) snprintf(value, sizeof value, "Результат вне диапазона отображения");
    else snprintf(value, sizeof value, "= %.6f", result.value);
    text(value, x + 20, y + 137, result.error == charm::math::Error::None ? kAmber : 0xffb5ac, clip, false, 16);
    text("+ − * / ^   sqrt() abs() sin() cos()", x + 20, y + 174, 0xb7b1a6, clip, true, 11);
  } else if (g_query[0] == '#') {
    uint8_t digest[64]; lib::crypto::sha512(g_query + 1, strlen(g_query + 1), digest);
    char hex[129]; constexpr char digits[] = "0123456789abcdef";
    for (unsigned i=0; i<64; ++i) { hex[2*i]=digits[digest[i]>>4]; hex[2*i+1]=digits[digest[i]&15]; } hex[128]=0;
    text("SHA-512 · UTF-8", x+20,y+116,kAmber,clip,false,12);
    for (unsigned i=0; i<4; ++i) { char line[33]; memcpy(line,hex+i*32,32); line[32]=0; text(line,x+20,y+144+static_cast<int32_t>(i)*22,kWhite,clip,true,11); }
  } else if (g_query[0] == '%' && g_query[1] == '%') {
    auto converted=charm::math::convert_query(g_query+2); char value[96];
    if (converted.valid && converted.value>=-1e12 && converted.value<=1e12) snprintf(value,sizeof value,"= %.6f",converted.value);
    else snprintf(value,sizeof value,"Формат: %%%% 10 m -> cm");
    text(value,x+20,y+137,converted.valid?kAmber:0xb7b1a6,clip,false,16);
    text("Длина · масса · время · температура",x+20,y+174,0x8e887d,clip,false,11);
  } else if (g_query[0] == '/') {
    text("/settings   Открыть настройки", x + 20, y + 133, kWhite, clip, false, 12);
    text("/shadows    Переключить тени", x + 20, y + 167, 0xb7b1a6, clip, false, 12);
    text("/warm       Переключить фон", x + 20, y + 201, 0xb7b1a6, clip, false, 12);
  } else {
    int n = launcher_count();
    int first = g_launcher_selected > 2 ? g_launcher_selected - 2 : 0;
    for (int i = first; i < n && i < first + 3; ++i) {
      int item = launcher_item(i); Rect r = launcher_rect(i);
      if (i == g_launcher_selected) rounded(r, 8, 0x29251b, clip);
      const char* title = item == 0 ? "Colors — демонстрация" : item == 1 ? "Файлы — Диск A" : item == 100 ? "Калькулятор" : item==101 ? "Редактор" : item==102 ? "Браузер — интернет" : g_win[item - 2].title;
      text(title, r.x + 12, r.y + 25, i == g_launcher_selected ? kAmber : kWhite, intersect(r, clip), false, 13);
    }
    if (n == 0) {
      text("Нет установленного приложения", x + 20, y + 134, 0xb7b1a6, clip, false, 13);
      text("Поиск Яндекс: требуется браузер", x + 20, y + 168, 0x8e887d, clip, false, 12);
    }
  }
  text(g_quick_notice[0] ? g_quick_notice : "Enter: выполнить   Esc: закрыть   F2: все", x + 18, y + 258, 0x8e887d, clip, false, 10);
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
  for (int row = 0; row < 3; ++row) {
    int32_t ty = y + 192 + row * 72;
    bool enabled = row == 0 ? g_shadows : row == 1 ? g_warm : g_russian;
    if (hot({x + 328, 248 + row * 72, 44, 24})) rounded({x + 320, ty - 3, 60, 40}, 16, 0x332e25, d);
    text(row == 0 ? "Тени окон" : row == 1 ? "Тёплый фон" : "Русский ввод поиска", x + 24, ty + 22, 0xf3efe7, d, false, 15);
    text(row == 0 ? "Мягкая тень под окнами" : row == 1 ? "Свечение за логотипом" : "F8 в панели поиска и в Файлах", x + 24, ty + 44, 0x8e887d, d, false, 12);
    rounded({x + 328, ty + 5, 44, 24}, 12, enabled ? kAmber : 0x3a3a3f, d);
    rounded({x + 331 + (enabled ? 20 : 0), ty + 8, 18, 18}, 9, enabled ? kInk : 0xd8d2c7, d);
  }
  text(g_preferences_saved ? "Настройки сохраняются на диске" : "Не удалось сохранить настройки", x + 24, y + 418, 0x8e887d, d, false, 12);
  text("Alt + Space   Универсальный доступ", x + 24, y + 456, 0xd8d2c7, d, true, 12);
  text("Alt + Tab     Переключить окно", x + 24, y + 482, 0xd8d2c7, d, true, 12);
  text("Alt + F4      Закрыть окно", x + 24, y + 508, 0xd8d2c7, d, true, 12);
  text("Alt + F9      Свернуть окно", x + 24, y + 534, 0xd8d2c7, d, true, 12);
  text("Esc           Закрыть панель", x + 24, y + 560, 0xd8d2c7, d, true, 12);
}

void launcher_draw(Rect d) {
  if (g_quick) { quick_draw(d); return; }
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
  const int ids[]={0,1,100,101,102};
  const bool available[]={demo_matches(),files_match(),calc_match(),edit_match(),browser_match()};
  const char* names[]={"Colors","Файлы","Калькулятор","Редактор","Браузер"};
  const char* descriptions[]={"Демонстрация окон","Просмотр файлов","Функции и графики","UTF-8 и исходный код","Интернет и веб-страницы"};
  for(int slot=0;slot<5;++slot) if(available[slot]) {
    Rect r=app_rect(slot); rounded(r,16,selected_item==ids[slot]?kAmber:0x332e25,d);
    rounded({r.x+1,r.y+1,r.w-2,r.h-2},15,kRaised,d);
    if(slot<2) tile_icon(r.x+20,r.y+22,slot,d);
    else if(slot==4) tile_icon(r.x+20,r.y+22,4,d);
    else text(slot==2?"=":"{}",r.x+20,r.y+56,kAmber,d,true,24);
    text(names[slot],r.x+20,r.y+104,0xf3efe7,d,false,15);
    text(descriptions[slot],r.x+20,r.y+130,0x8e887d,d,false,12);
    if(selected_item==ids[slot]) text("ENTER",r.x+122,r.y+46,kAmber,d,true,11);
  }
  if(launcher_count()==0) text("Ничего не найдено",x+20,y+184,0x8e887d,d);
  text("ОТКРЫТЫЕ ОКНА",x,window_cards_y()-20,0x8e887d,d,false,11,1);
  for (int i = 0; launcher_item(i) >= 0; ++i) {
    int item = launcher_item(i);
    if (item < 2 || item >= 100) continue;
    Window& w = g_win[item - 2];
    Rect r = launcher_rect(i); int32_t tx = r.x, ty = r.y;
    rounded(r, 10, i == g_launcher_selected ? kAmber : 0x2a2a2e, d);
    rounded({tx + 1, ty + 1, 198, 46}, 9, 0x141416, d);
    text(w.title, tx + 16, ty + (w.minimized ? 20 : 29), 0xf3efe7, intersect({tx + 8, ty, 184, 48}, d), false, 14);
    if (w.minimized) text("Свёрнуто", tx + 16, ty + 38, 0x8e887d, d, false, 10);
  }
}

Rect content(const Window& w) { return {w.frame.x, w.frame.y + kTitle, w.w, w.h}; }

void damage(Rect r) {
  if (r.empty()) return;
  Rect expanded{r.x - 16, r.y - 16, r.w + 32, r.h + 40};
  g_damage = unite(g_damage, expanded);
  Rect clipped = intersect(expanded, g_s.all());
  if (clipped.empty()) return;
  for (unsigned i = 0; i < g_region_count; ++i) {
    Rect merged = unite(g_regions[i], clipped);
    int64_t cost = int64_t{g_regions[i].w} * g_regions[i].h + int64_t{clipped.w} * clipped.h;
    if (int64_t{merged.w} * merged.h <= cost) { g_regions[i] = merged; return; }
  }
  if (g_region_count < 16) g_regions[g_region_count++] = clipped;
  else { g_regions[0] = intersect(g_damage, g_s.all()); g_region_count = 1; }
}

void rearm_client(int ci) {
  Client& client=g_cl[ci];
  if(!client.used || client.failed) return;
  uint64_t key=kKeyClient+static_cast<uint64_t>(ci);
  (void)charm::object_unbind(g_port,client.ch,key);
  uint32_t mask=client.output_count ? 3u : 1u;
  if(charm::object_watch(g_port,client.ch,key,mask)!=Status::Ok) client.failed=true;
}
void flush_client(int ci) {
  Client& client=g_cl[ci];
  while(client.used && !client.failed && client.output_count) {
    OutboundMessage& next=client.output[client.output_head];
    auto status=charm::channel_write(client.ch,next.bytes,next.size,
        next.capability ? &next.capability : nullptr,next.capability ? 1 : 0);
    if(status==Status::WouldBlock) return;
    if(status!=Status::Ok) { client.failed=true; return; }
    next.capability=0;
    client.output_head=(client.output_head+1)%kOutboundMessages;
    --client.output_count;
  }
}
// On success this takes ownership of capability, including a deferred send.
Status send_message(int ci,const void* data,uint32_t size,Handle capability=0) {
  Client& client=g_cl[ci];
  if(!client.used || client.failed) return Status::PeerClosed;
  if(size>sizeof(OutboundMessage::bytes)) return Status::InvalidArgs;
  if(client.output_count==kOutboundMessages) { client.failed=true; return Status::NoSpace; }
  bool previously_empty=!client.output_count;
  auto& next=client.output[(client.output_head+client.output_count)%kOutboundMessages];
  next.size=size; next.capability=capability;
  memcpy(next.bytes,data,size);
  ++client.output_count;
  flush_client(ci);
  if(previously_empty && client.output_count) rearm_client(ci);
  return Status::Ok;
}
void send_event(int client,uint32_t type,uint32_t window,uint32_t value,uint32_t generation=0) {
  if(client<0 || !g_cl[client].used || g_cl[client].failed) return;
  wm::WindowEventV3 event{{type,window,value},generation,0,0,0,0};
  if(g_cl[client].version==wm::kManagedVersion) {
    for(const auto& candidate:g_win) if(candidate.used && candidate.client==client && candidate.id==window) {
      if(!generation) event.generation=candidate.generation;
      event.x=candidate.frame.x; event.y=candidate.frame.y+kTitle;
      event.width=static_cast<uint32_t>(candidate.w); event.height=static_cast<uint32_t>(candidate.h);
      break;
    }
  }
  uint32_t size=g_cl[client].version==wm::kManagedVersion ? sizeof event : sizeof event.base;
  if(send_message(client,&event,size)!=Status::Ok) g_cl[client].failed=true;
}

void raise(int wi) {
  int at = -1;
  for (int i = 0; i < g_zn; ++i) {
    if (g_z[i] == wi) at = i;
  }
  if (at < 0) return;
  if (g_win[wi].minimized) {
    g_win[wi].minimized = false;
    if(g_cl[g_win[wi].client].version==wm::kManagedVersion)
      send_event(g_win[wi].client,wm::kVisibilityChanged,g_win[wi].id,1);
    printf("wm: window %u restored\n", g_win[wi].id);
  }
  for (int i = at; i + 1 < g_zn; ++i) g_z[i] = g_z[i + 1];
  g_z[g_zn - 1] = wi;
  damage(g_win[wi].frame);
}

void release_capture() {
  if(g_capture<0) return;
  int wi=g_capture;
  g_capture=-1;
  if(g_win[wi].used) send_event(g_win[wi].client,wm::kCaptureChanged,g_win[wi].id,0);
}

void set_focus(int wi) {
  if (wi == g_focus) return;
  if(g_capture>=0 && g_capture!=wi) release_capture();
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
    if (!g_win[g_z[i]].minimized && g_win[g_z[i]].frame.contains(x, y)) return g_z[i];
  }
  return -1;
}

Rect hover_rect() {
  int x = g_cursor.x, y = g_cursor.y;
  Rect header[] = {{100, 4, 150, 30}, {250, 4, 70, 30},
    {static_cast<int32_t>(g_s.width) - 262, 9, 100, 23},
    {static_cast<int32_t>(g_s.width) - 150, 9, 132, 23}};
  for (Rect r : header) if (r.contains(x, y)) return r;
  if (g_control) {
    for (int i = 0; i < 3; ++i) {
      Rect r{static_cast<int32_t>(g_s.width) - 104, 248 + i * 72, 44, 24};
      if (r.contains(x, y)) return r;
    }
    return {};
  }
  if (g_launcher || g_drag >= 0) return {};
  int wi = window_at(x, y);
  if (wi >= 0) {
    Rect frame = g_win[wi].frame;
    Rect buttons[] = {{frame.x + 16, frame.y + 16, 20, 20}, {frame.x + frame.w - 36, frame.y + 16, 20, 20}};
    for (Rect r : buttons) if (r.contains(x, y)) return r;
  } else {
    for (int i = 0; i < 3; ++i) {
      Rect r{47 + i * 122, static_cast<int32_t>(g_s.height) - 152, 114, 104};
      if (r.contains(x, y)) return r;
    }
  }
  return {};
}

void focus_top_visible() {
  for (int i = g_zn - 1; i >= 0; --i) {
    if (!g_win[g_z[i]].minimized) { set_focus(g_z[i]); return; }
  }
  set_focus(-1);
}

void minimize(int wi) {
  if (wi < 0 || !g_win[wi].used || g_win[wi].minimized) return;
  Window& w = g_win[wi];
  if(g_capture==wi) release_capture();
  w.minimized = true;
  if(g_cl[w.client].version==wm::kManagedVersion) send_event(w.client,wm::kVisibilityChanged,w.id,0);
  if (g_drag == wi) g_drag = -1;
  damage(w.frame);
  if (g_focus == wi) focus_top_visible();
  printf("wm: window %u minimized\n", w.id);
}

void destroy_window(int wi) {
  Window& w = g_win[wi];
  bool focused = g_focus == wi;
  if(g_capture==wi) release_capture();
  if (focused) set_focus(-1);
  printf("wm: window %u destroyed\n", w.id);
  damage(w.frame);
  int k = 0;
  for (int i = 0; i < g_zn; ++i) {
    if (g_z[i] != wi) g_z[k++] = g_z[i];
  }
  g_zn = k;
  if (g_focus == wi) g_focus = -1;
  if (g_drag == wi) g_drag = -1;
  if (g_browser_window == wi) g_browser_window = -1;
  for (auto& buf : w.buf) {
    if (buf != nullptr) (void)charm::vmo_unmap(reinterpret_cast<uint64_t>(buf));
    buf = nullptr;
  }
  if (w.ring != nullptr) (void)charm::vmo_unmap(reinterpret_cast<uint64_t>(w.ring));
  w.ring = nullptr; w.used = false;
  if (focused) focus_top_visible();
}

void drop_client(int ci) {
  if(!g_cl[ci].used) return;
  g_cl[ci].used=false;
  (void)charm::object_unbind(g_port,g_cl[ci].ch,kKeyClient+static_cast<uint64_t>(ci));
  for(auto& message:g_cl[ci].output) if(message.capability) {
    (void)charm::handle_close(message.capability); message.capability=0;
  }
  for (int i = 0; i < kMaxWindows; ++i) {
    if (g_win[i].used && g_win[i].client == ci) destroy_window(i);
  }
  (void)charm::handle_close(g_cl[ci].ch);
  g_cl[ci]={};
}
void collect_failed_clients() {
  for(int pass=0;pass<kMaxClients;++pass) {
    bool found=false;
    for(int ci=0;ci<kMaxClients;++ci) if(g_cl[ci].used && g_cl[ci].failed) {
      drop_client(ci); found=true;
    }
    if(!found) return;
  }
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

bool map_framebuffers(const Handle* handles,uint32_t count,uint64_t bytes,uint64_t* first,uint64_t* second) {
  *first=*second=0;
  if(count!=2) return false;
  auto size0=charm::vmo_size(handles[0]),size1=charm::vmo_size(handles[1]);
  if(!size0.ok() || !size1.ok() || size0.value<bytes || size1.value<bytes) return false;
  auto a=charm::vmo_map(handles[0],charm::rights::kRead);
  if(!a.ok()) return false;
  auto b=charm::vmo_map(handles[1],charm::rights::kRead);
  if(!b.ok()) { (void)charm::vmo_unmap(a.value); return false; }
  *first=a.value; *second=b.value;
  return true;
}
void clamp_position(Window& window,int32_t content_x,int32_t content_y) {
  int64_t x=content_x,y=int64_t{content_y}-kTitle;
  int64_t maximum_x=int64_t{g_s.width}-window.w;
  int64_t maximum_y=int64_t{g_s.height}-window.h-kTitle;
  if(maximum_y<kPanel) maximum_y=kPanel;
  if(x<0) x=0;
  if(x>maximum_x) x=maximum_x;
  if(y<kPanel) y=kPanel;
  if(y>maximum_y) y=maximum_y;
  window.frame.x=static_cast<int32_t>(x); window.frame.y=static_cast<int32_t>(y);
}
void create_window(int ci, const wm::Create& m, const Handle* hs, uint32_t nh,uint32_t flags=0) {
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
  if(g_cl[ci].version==wm::kManagedVersion && uint64_t{m.height}+kTitle+kPanel>g_s.height) ok=false;
  uint64_t map0 = 0, map1 = 0;
  if(ok) ok=map_framebuffers(hs,nh,bytes,&map0,&map1);
  // Кольцо ввода окна: создаём, отдаём клиенту дубликат (он двигает head).
  auto ring = ok ? charm::vmo_create(4096) : charm::SysResult{Status::NoMemory, 0};
  auto ring_map = ring.ok() ? charm::vmo_map(static_cast<Handle>(ring.value), charm::rights::kRead | charm::rights::kWrite)
                            : charm::SysResult{Status::NoMemory, 0};
  for (uint32_t i = 0; i < nh; ++i) (void)charm::handle_close(hs[i]);  // отображения держат VMO
  if (!ok || !ring_map.ok()) {
    if(map0) (void)charm::vmo_unmap(map0);
    if(map1) (void)charm::vmo_unmap(map1);
    if(ring_map.ok()) (void)charm::vmo_unmap(ring_map.value);
    if(ring.ok()) (void)charm::handle_close(static_cast<Handle>(ring.value));
    (void)send_message(ci,&reply,sizeof reply);
    return;
  }
  Window& w = g_win[wi];
  memset(&w, 0, sizeof w);
  w.used = true;
  w.id = g_next_id++;
  w.generation=g_cl[ci].version==wm::kManagedVersion ? 1 : 0;
  w.client = ci;
  w.minimized=(flags&wm::kCreateInvisible)!=0;
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
  if(g_cl[ci].version==wm::kManagedVersion) clamp_position(w,w.frame.x,w.frame.y+kTitle);
  w.buf[0] = reinterpret_cast<const uint32_t*>(map0);
  w.buf[1] = reinterpret_cast<const uint32_t*>(map1);
  w.shown = w.pending = -1;
  w.ring = reinterpret_cast<sc::InputRingHeader*>(ring_map.value);
  w.events = reinterpret_cast<sc::InputEvent*>(ring_map.value + sizeof(sc::InputRingHeader));
  memcpy(w.ring->magic, "INPR", 4);
  w.ring->version = 1;
  w.ring->capacity = 128;
  g_z[g_zn++] = wi;
  if (g_browser_window < 0 &&
      (strstr(w.title, "Chromium") != nullptr || strstr(w.title, "CharmOS") != nullptr)) {
    g_browser_window = wi;
  }
  damage(w.frame);
  reply.window = w.id;
  reply.x = w.frame.x;
  reply.y = w.frame.y + kTitle;
  Handle give = static_cast<Handle>(ring.value);
  if(send_message(ci,&reply,sizeof reply,give)!=Status::Ok) {
    (void)charm::handle_close(give);
    g_cl[ci].failed=true;
    return;
  }
  if(!w.minimized && !(flags&wm::kCreateInactive)) set_focus(wi);
  printf("wm: window %u \"%s\" %ux%u at %d,%d\n", w.id, w.title, m.width, m.height, w.frame.x, w.frame.y);
}

int find_window(int ci, uint32_t id) {
  for (int i = 0; i < kMaxWindows; ++i) {
    if (g_win[i].used && g_win[i].client == ci && g_win[i].id == id) return i;
  }
  return -1;
}

void configure_window(int ci,const wm::Configure& request,const Handle* handles,uint32_t count) {
  int wi=find_window(ci,request.window);
  bool shape=wi>=0 && count==2 && !request.reserved &&
      g_win[wi].generation!=UINT32_MAX && request.generation==g_win[wi].generation+1;
  if(!shape) {
    for(uint32_t i=0;i<count;++i) (void)charm::handle_close(handles[i]);
    g_cl[ci].failed=true;
    return;
  }
  Window& window=g_win[wi];
  bool valid=request.width && request.height && request.width<=g_s.width &&
      uint64_t{request.height}+kTitle+kPanel<=g_s.height;
  uint64_t first=0,second=0;
  if(valid) valid=map_framebuffers(handles,count,uint64_t{request.width}*request.height*4,&first,&second);
  for(uint32_t i=0;i<count;++i) (void)charm::handle_close(handles[i]);
  if(!valid) { send_event(ci,wm::kConfigured,window.id,1,request.generation); return; }
  if(window.pending>=0) send_event(ci,wm::kFrameDone,window.id,static_cast<uint32_t>(window.pending));
  damage(window.frame);
  for(auto buffer:window.buf) if(buffer) (void)charm::vmo_unmap(reinterpret_cast<uint64_t>(buffer));
  window.buf[0]=reinterpret_cast<const uint32_t*>(first);
  window.buf[1]=reinterpret_cast<const uint32_t*>(second);
  window.w=static_cast<int32_t>(request.width); window.h=static_cast<int32_t>(request.height);
  window.frame.w=window.w; window.frame.h=window.h+kTitle;
  window.generation=request.generation;
  window.shown=window.pending=-1; window.pending_damage={};
  clamp_position(window,request.x,request.y);
  damage(window.frame);
  send_event(ci,wm::kConfigured,window.id,0);
}

void client_message(int ci) {
  alignas(4) uint8_t msg[128];
  Handle hs[4];
  for (unsigned messages=0;messages<64 && !g_cl[ci].failed;++messages) {
    auto r = charm::channel_read(g_cl[ci].ch, msg, sizeof msg, hs, 4, 0);
    if (r.status == Status::WouldBlock) return;
    if (!r.ok()) {
      g_cl[ci].failed=true;
      return;
    }
    uint32_t len = static_cast<uint32_t>(r.value >> 32), nh = static_cast<uint32_t>(r.value & 0xffffffff);
    uint32_t type = len >= 4 ? *reinterpret_cast<uint32_t*>(msg) : 0;
    bool managed=g_cl[ci].version==wm::kManagedVersion;
    if(type==wm::kCreate) {
      if(!managed && len==sizeof(wm::Create)) {
        create_window(ci,*reinterpret_cast<wm::Create*>(msg),hs,nh);
        continue;
      }
      if(managed && len==sizeof(wm::CreateV3)) {
        auto& request=*reinterpret_cast<wm::CreateV3*>(msg);
        if(nh==2 && !(request.flags&~(wm::kCreateInvisible|wm::kCreateInactive))) {
          create_window(ci,request.base,hs,nh,request.flags);
          continue;
        }
      }
    }
    if(managed && type==wm::kConfigure && len==sizeof(wm::Configure)) {
      configure_window(ci,*reinterpret_cast<wm::Configure*>(msg),hs,nh);
      continue;
    }
    for (uint32_t i = 0; i < nh; ++i) (void)charm::handle_close(hs[i]);
    if(nh) { g_cl[ci].failed=true; return; }
    if (type == wm::kPresent && len == (managed ? sizeof(wm::PresentV3) : sizeof(wm::Present))) {
      auto& p = *reinterpret_cast<wm::Present*>(msg);
      int wi = find_window(ci, p.window);
      if (wi < 0 || p.buffer > 1 || g_win[wi].shown == static_cast<int>(p.buffer) ||
          g_win[wi].pending==static_cast<int>(p.buffer) ||
          (managed && reinterpret_cast<wm::PresentV3*>(msg)->generation!=g_win[wi].generation)) {
        g_cl[ci].failed=true;
        return;
      }
      Window& w = g_win[wi];
      if(p.x<0 || p.y<0 || p.w<=0 || p.h<=0 || int64_t{p.x}+p.w>w.w || int64_t{p.y}+p.h>w.h) {
        g_cl[ci].failed=true; return;
      }
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
      else { g_cl[ci].failed=true; return; }
    } else if(managed && type==wm::kSetTitle && len==sizeof(wm::SetTitle)) {
      const auto& request=*reinterpret_cast<wm::SetTitle*>(msg);
      int wi=find_window(ci,request.window);
      if(wi<0 || !memchr(request.title,0,sizeof request.title)) { g_cl[ci].failed=true; return; }
      memcpy(g_win[wi].title,request.title,sizeof request.title);
      damage(g_s.all());
    } else if(managed && type==wm::kSetPosition && len==sizeof(wm::SetPosition)) {
      const auto& request=*reinterpret_cast<wm::SetPosition*>(msg);
      int wi=find_window(ci,request.window);
      if(wi<0) { g_cl[ci].failed=true; return; }
      damage(g_win[wi].frame);
      clamp_position(g_win[wi],request.x,request.y);
      damage(g_win[wi].frame);
      send_event(ci,wm::kBoundsChanged,request.window,0);
    } else if(managed && type==wm::kSetVisibility && len==sizeof(wm::SetVisibility)) {
      const auto& request=*reinterpret_cast<wm::SetVisibility*>(msg);
      int wi=find_window(ci,request.window);
      if(wi<0 || request.visible>1 || request.activate>1) { g_cl[ci].failed=true; return; }
      bool was_minimized=g_win[wi].minimized;
      if(request.visible) {
        raise(wi);
        if(!was_minimized) send_event(ci,wm::kVisibilityChanged,request.window,1);
        if(request.activate) set_focus(wi);
      } else {
        minimize(wi);
        if(was_minimized) send_event(ci,wm::kVisibilityChanged,request.window,0);
      }
    } else if(managed && type==wm::kActivate && len==sizeof(wm::Activate)) {
      const auto& request=*reinterpret_cast<wm::Activate*>(msg);
      int wi=find_window(ci,request.window);
      if(wi<0 || request.active>1) { g_cl[ci].failed=true; return; }
      if(request.active) { raise(wi); set_focus(wi); }
      else if(g_focus==wi) set_focus(-1);
    } else if(managed && type==wm::kSetCapture && len==sizeof(wm::SetCapture)) {
      const auto& request=*reinterpret_cast<wm::SetCapture*>(msg);
      int wi=find_window(ci,request.window);
      if(wi<0 || request.captured>1) { g_cl[ci].failed=true; return; }
      if(request.captured && g_focus==wi && !g_win[wi].minimized && !g_launcher && !g_control && g_drag<0) {
        if(g_capture!=wi) release_capture();
        g_capture=wi;
      } else if(!request.captured && g_capture==wi) {
        release_capture();
        continue;
      }
      send_event(ci,wm::kCaptureChanged,request.window,g_capture==wi ? 1u : 0u);
    } else {
      g_cl[ci].failed=true;
      return;
    }
  }
}

void door_message(Handle door) {
  wm::Helo m;
  Handle hs[1];
  for (unsigned messages=0;messages<64;++messages) {
    auto r = charm::channel_read(door, &m, sizeof m, hs, 1, 0);
    if (!r.ok()) return;
    uint32_t len = static_cast<uint32_t>(r.value >> 32), nh = static_cast<uint32_t>(r.value & 0xffffffff);
    if (len != sizeof m || m.type != wm::kHelo ||
        (m.version!=wm::kVersion && m.version!=wm::kManagedVersion) || nh != 1) {
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
    g_cl[ci]={};
    g_cl[ci].used=true; g_cl[ci].ch=hs[0]; g_cl[ci].version=m.version;
    wm::WelcomeV3 welcome{{wm::kWelcome,m.version,g_s.bgrx ? sc::kDisplayBgrx : sc::kDisplayRgbx,
                          g_s.width,g_s.height},kTitle,0,kPanel,g_s.width,g_s.height-kPanel,
                         wm::kFeatureTitle|wm::kFeatureBounds|wm::kFeatureVisibility|wm::kFeatureActivation|
                         wm::kFeatureCapture};
    uint32_t bytes=m.version==wm::kManagedVersion ? sizeof welcome : sizeof welcome.base;
    if(send_message(ci,&welcome,bytes)!=Status::Ok) g_cl[ci].failed=true;
    rearm_client(ci);
  }
}

void launcher_activate() {
  int item = launcher_item(g_launcher_selected);
  if (item == 0) launch_demo();
  else if (item == 1) launch_files();
  else if (item == 100) launch_calc();
  else if (item == 101) launch_edit();
  else if (item == 102) launch_browser();
  else if (item >= 2) { raise(item - 2); set_focus(item - 2); launcher_toggle(); }
}

// Ввод из ядра: курсор, перетаскивание, фокус; остальное — окну.
void input_event(const sc::InputEvent& e) {
  if (e.type == sc::kInputKey) {
    if (e.code == 0xe1 || e.code == 0xe5) {
      unsigned bit = e.code == 0xe1 ? 1u : 2u;
      if (e.value != 0) g_shift |= bit; else g_shift &= ~bit;
      if (g_launcher) return;
    }
    if (g_control && e.value != 0 && e.code == 0x29) { control_toggle(); return; }
    if (e.code == 0xe2 || e.code == 0xe6) {
      unsigned bit=e.code==0xe2 ? 1u : 2u;
      if(e.value) g_alt|=bit; else g_alt&=~bit;
    }
    if (g_alt && e.value == 1 && e.code == 0x2c) { launcher_toggle(true); return; }
    if (g_alt && e.value == 1 && e.code == 0x5) { launch_browser(); return; }
    if (g_launcher && g_alt && e.value == 1 && e.code == 0x3d) { launcher_toggle(); return; }
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
      if (e.code == 0x41) { g_russian = !g_russian; save_preferences(); damage(g_s.all()); return; }
      if (e.code == 0x3b) { g_quick = false; damage(g_s.all()); return; }
      if (e.code == 0x28 && g_query[0] == '=') {
        auto result = charm::math::evaluate(g_query + 1);
        if (result.error == charm::math::Error::None && result.value >= -1e12 && result.value <= 1e12) {
          snprintf(g_quick_notice, sizeof g_quick_notice, "Результат: %.6f", result.value);
          printf("wm: calculate %.6f\n", result.value);
        }
        damage(g_s.all()); return;
      }
      if (e.code == 0x28 && g_query[0] == '%' && g_query[1] == '%') {
        auto converted=charm::math::convert_query(g_query+2);
        if (converted.valid && converted.value>=-1e12 && converted.value<=1e12) printf("wm: convert %.6f\n",converted.value);
        return;
      }
      if (e.code == 0x28 && g_query[0] == '#') {
        uint8_t digest[64]; lib::crypto::sha512(g_query+1,strlen(g_query+1),digest);
        char prefix[17]; constexpr char digits[]="0123456789abcdef";
        for(unsigned i=0;i<8;++i) { prefix[2*i]=digits[digest[i]>>4]; prefix[2*i+1]=digits[digest[i]&15]; } prefix[16]=0;
        printf("wm: SHA-512 %s\n",prefix); return;
      }
      if (e.code == 0x28 && g_query[0] == '/') {
        if (strcmp(g_query, "/settings") == 0) { control_toggle(); return; }
        if (strcmp(g_query, "/shadows") == 0) { g_shadows = !g_shadows; }
        else if (strcmp(g_query, "/warm") == 0) { g_warm = !g_warm; wallpaper_build(); }
        else { snprintf(g_quick_notice, sizeof g_quick_notice, "Неизвестная команда"); damage(g_s.all()); return; }
        save_preferences();
        damage(g_s.all()); return;
      }
      if (e.code == 0x29) { launcher_toggle(); return; }
      if (e.code == 0x28) { launcher_activate(); return; }
      if (e.code == 0x2b || (e.code >= 0x4f && e.code <= 0x52)) {
        int n = launcher_count();
        int step = e.code == 0x50 || e.code == 0x52 || (e.code == 0x2b && g_shift != 0) ? -1 : 1;
        if (n > 0) g_launcher_selected = (g_launcher_selected + n + step) % n;
        damage(g_s.all()); return;
      }
      if (e.code == 0x2a) charm::ui::backspace(g_query, g_query_len);
      else if (!charm::ui::append(g_query, sizeof g_query, g_query_len, charm::ui::key(e.code, g_shift != 0, g_russian))) return;
      g_launcher_selected = 0;
      damage(g_s.all()); return;
    }
    if (g_alt && e.value == 1 && e.code == 0x42) { minimize(g_focus); return; }
    if (g_control) return;
  }
  if (e.type == sc::kInputMotion) {
    Rect old = g_cursor;
    Rect old_hot = hover_rect();
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
      if(g_cl[w.client].version==wm::kManagedVersion) {
        clamp_position(w,w.frame.x,w.frame.y+kTitle);
        send_event(w.client,wm::kBoundsChanged,w.id,0);
      }
      damage(w.frame);
    }
    Rect next_hot = hover_rect();
    if (old_hot.x != next_hot.x || old_hot.y != next_hot.y || old_hot.w != next_hot.w || old_hot.h != next_hot.h) {
      damage(old_hot); damage(next_hot);
    }
    if (g_drag < 0 && !g_launcher && !g_control) {
      int wi = g_capture>=0 ? g_capture : window_at(g_cursor.x, g_cursor.y);
      if (wi >= 0 && (g_capture==wi || content(g_win[wi]).contains(g_cursor.x, g_cursor.y))) {
        auto fe = e; Rect c = content(g_win[wi]);
        fe.x = g_cursor.x - c.x; fe.y = g_cursor.y - c.y; forward(wi, fe);
      }
    }
    return;
  }
  if(g_capture>=0 && (e.type==sc::kInputButton || e.type==sc::kInputWheel)) {
    auto event=e;
    Rect bounds=content(g_win[g_capture]);
    event.x=g_cursor.x-bounds.x; event.y=g_cursor.y-bounds.y;
    forward(g_capture,event);
    return;
  }
  if (e.type == sc::kInputButton && e.value == 1 && e.code == 1 && g_cursor.y < kPanel) {
    if (g_cursor.x >= static_cast<int32_t>(g_s.width) - 262 && g_cursor.x < static_cast<int32_t>(g_s.width) - 162) control_toggle();
    else if (g_cursor.x >= static_cast<int32_t>(g_s.width) - 150 && g_cursor.x < static_cast<int32_t>(g_s.width) - 18) launcher_toggle();
    else if (g_cursor.x >= 100 && g_cursor.x < 250) launcher_toggle();
    else if (g_cursor.x >= 250 && g_cursor.x < 320 && g_zn > 0) { int wi = g_z[0]; raise(wi); set_focus(wi); }
    return;
  }
  if (g_control) {
    if (e.type == sc::kInputButton && e.code == 1 && e.value == 1) {
      int32_t x = static_cast<int32_t>(g_s.width) - 432;
      if (Rect{x + 328, 248, 44, 24}.contains(g_cursor.x, g_cursor.y)) {
        g_shadows = !g_shadows; save_preferences(); damage(g_s.all()); printf("wm: shadows %s\n", g_shadows ? "on" : "off");
      } else if (Rect{x + 328, 320, 44, 24}.contains(g_cursor.x, g_cursor.y)) {
        g_warm = !g_warm; save_preferences(); wallpaper_build(); damage(g_s.all()); printf("wm: warm %s\n", g_warm ? "on" : "off");
      } else if (Rect{x + 328, 392, 44, 24}.contains(g_cursor.x, g_cursor.y)) {
        g_russian=!g_russian; save_preferences(); damage(g_s.all());
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
  if (e.type == sc::kInputButton && e.code == 1 && e.value == 1 &&
      Rect{static_cast<int32_t>(g_s.width) - 424, static_cast<int32_t>(g_s.height) - 192, 392, 160}.contains(g_cursor.x, g_cursor.y) && window_at(g_cursor.x, g_cursor.y) < 0) {
    launcher_toggle(true); return;
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
          if (e.code == 1 && Rect{g_win[wi].frame.x + g_win[wi].frame.w - 36, g_win[wi].frame.y + 16, 20, 20}.contains(g_cursor.x, g_cursor.y)) {
            minimize(wi); return;
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
void compose_region(Rect d) {
  if (d.empty()) return;
  if (g_wallpaper != nullptr) {
    for (int32_t y = d.y; y < d.y + d.h; ++y)
      memcpy(g_s.back + uint64_t{static_cast<uint32_t>(y)} * g_s.width + d.x,
             g_wallpaper + uint64_t{static_cast<uint32_t>(y)} * g_s.width + d.x, static_cast<size_t>(d.w) * 4);
  } else g_s.fill(d, kInk, d);
  desktop(d);
  for (int i = 0; i < g_zn; ++i) {
    Window& w = g_win[g_z[i]];
    if (w.minimized) continue;
    Rect bar{w.frame.x, w.frame.y, w.frame.w, kTitle};
    if (g_shadows) {
      shadow({bar.x - 12, bar.y - 4, w.frame.w + 24, w.frame.h + 24}, w.frame, 28, 24, d);
      shadow({bar.x - 6, bar.y, w.frame.w + 12, w.frame.h + 12}, w.frame, 22, 40, d);
    }
    rounded({bar.x - 1, bar.y - 1, w.frame.w + 2, w.frame.h + 2}, 18, g_z[i] == g_focus ? 0x5b5140 : 0x2a2a2e, d);
    rounded(w.frame, 17, 0x17171a, d);
    g_s.fill({bar.x, bar.y + kTitle - 1, bar.w, 1}, 0x2a2a2e, d);
    if (hot({bar.x + 16, bar.y + 16, 20, 20})) rounded({bar.x + 12, bar.y + 12, 28, 28}, 7, 0x542522, d);
    rounded({bar.x + 16, bar.y + 20, 12, 12}, 3, hot({bar.x + 16, bar.y + 16, 20, 20}) ? 0xff6559 : 0xe0352b, d);
    rounded({bar.x + bar.w - 36, bar.y + 16, 20, 20}, 5, hot({bar.x + bar.w - 36, bar.y + 16, 20, 20}) ? 0x51452a : 0x29251b, d);
    g_s.fill({bar.x + bar.w - 31, bar.y + 26, 10, 2}, kAmber, d);
    text(w.title, bar.x + 44, bar.y + 31, g_z[i] == g_focus ? 0xf3efe7 : 0x8e887d,
         intersect({bar.x + 40, bar.y, bar.w - 84, bar.h}, d), false, 14);
    Rect c = intersect(intersect(content(w), d), g_s.all());
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

void compose() {
  uint64_t started = charm::now_ns();
  g_hot = hover_rect();
  Rect regions[16]; unsigned n = g_region_count;
  for (unsigned i = 0; i < n; ++i) regions[i] = g_regions[i];
  g_region_count = 0; g_damage = {};
  static uint64_t frames = 0, time = 0, pixels = 0;
  for (unsigned i = 0; i < n; ++i) {
    compose_region(regions[i]);
    pixels += static_cast<uint64_t>(regions[i].w) * static_cast<uint64_t>(regions[i].h);
  }
  time += charm::now_ns() - started;
  if (++frames == 120) {
    printf("wm: compose avg %llu us, %llu pixels\n", static_cast<unsigned long long>(time / frames / 1000), static_cast<unsigned long long>(pixels / frames));
    frames = 0; time = 0; pixels = 0;
  }
}

}  // namespace

extern "C" int main(int, char**) {
  Handle display = charm_display(), input = charm_input();
  Handle door = charm_extra_handle(0);
  g_wm_door = charm_wm() != 0 ? charm_wm() : door;
  g_init_ctl = charm_extra_handle(1);  // 0 — дверь, 1 — канал управления init
  printf("wm: init control %s\n", g_init_ctl != 0 ? "yes" : "no");
  printf("wm: client capability %s\n", charm_wm() != 0 ? "yes" : "no");  // wm.md §1: конец двери — хендл без роли
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
  g_calc_available = stat("/bin/calc", &files_stat) == 0 && S_ISREG(files_stat.st_mode);
  g_edit_available = stat("/bin/edit", &files_stat) == 0 && S_ISREG(files_stat.st_mode);
  g_browser_available = stat("/bin/content_shell", &files_stat) == 0 && S_ISREG(files_stat.st_mode);
  load_preferences();
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
    poll_browser_focus();
    if (ring != nullptr) (void)charm::object_bind(g_port, input, kKeyInput);
    uint64_t deadline = charm::kForever;
    if (!g_damage.empty()) deadline = last_frame + kFrameNs;
    if (g_browser_focus_pending) {
      // Retry the pending card focus while the browser service is still
      // starting, without spinning: wake up a few times per second.
      uint64_t retry = charm::now_ns() + 50'000'000ull;
      if (deadline == charm::kForever || retry < deadline) deadline = retry;
    }
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
          flush_client(ci);
          if(!g_cl[ci].failed) client_message(ci);
          flush_client(ci);
          rearm_client(ci);
        }
      }
    }
    collect_failed_clients();
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
          // Buffer ownership advances even for hidden or off-screen windows.
          g_win[i].shown = g_win[i].pending;
          g_win[i].pending = -1;
          g_win[i].pending_damage = {};
        }
      }
      compose();
      last_frame = now;
      for (int k = 0; k < nd; ++k) {
        Window& w = g_win[done_win[k]];
        send_event(w.client, wm::kFrameDone, w.id, static_cast<uint32_t>(done_buf[k]));
      }
    }
    collect_failed_clients();
  }
}
