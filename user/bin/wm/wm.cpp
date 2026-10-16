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
#include <stdio.h>
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
constexpr int32_t kTitle = 24;   // высота заголовка окна
constexpr int32_t kCursor = 8;   // курсор — квадрат
constexpr uint64_t kFrameNs = 16'666'667;  // не чаще 60 Гц (wm.md §3)
constexpr int kMaxClients = 8, kMaxWindows = 16;
constexpr uint64_t kKeyInput = 1, kKeyDoor = 2, kKeyClient = 100;

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

Rect content(const Window& w) { return {w.frame.x, w.frame.y + kTitle, w.w, w.h}; }

void damage(Rect r) { g_damage = unite(g_damage, r); }

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
  damage(w.frame);
  int k = 0;
  for (int i = 0; i < g_zn; ++i) {
    if (g_z[i] != wi) g_z[k++] = g_z[i];
  }
  g_zn = k;
  if (g_focus == wi) g_focus = -1;
  if (g_drag == wi) g_drag = -1;
  w.used = false;  // отображения буферов остаются до выхода (памяти хватает; см. LAYOUT — позже)
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
  __atomic_store_n(&w.ring->tail, tail + 1, __ATOMIC_RELEASE);
  if (tail == head) send_event(w.client, wm::kInput, w.id, 0);
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

// Ввод из ядра: курсор, перетаскивание, фокус; остальное — окну.
void input_event(const sc::InputEvent& e) {
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
    if (g_drag >= 0) {
      Window& w = g_win[g_drag];
      damage(w.frame);
      w.frame.x += g_cursor.x - old.x;
      w.frame.y += g_cursor.y - old.y;
      damage(w.frame);
    }
    return;
  }
  if (e.type == sc::kInputButton) {
    if (e.value != 0) {
      int wi = window_at(g_cursor.x, g_cursor.y);
      if (wi >= 0) {
        raise(wi);
        set_focus(wi);
        if (g_cursor.y < g_win[wi].frame.y + kTitle) {
          g_drag = wi;  // за заголовок — тащим (событие окну не идёт)
          return;
        }
      }
    } else if (g_drag >= 0) {
      printf("wm: window %u moved to %d,%d\n", g_win[g_drag].id, g_win[g_drag].frame.x, g_win[g_drag].frame.y);
      g_drag = -1;
      return;
    }
  }
  if (g_focus < 0) return;
  sc::InputEvent fe = e;
  Rect c = content(g_win[g_focus]);
  fe.x = g_cursor.x - c.x;
  fe.y = g_cursor.y - c.y;
  if (e.type == sc::kInputKey) {
    fe.x = fe.y = 0;
    if (e.value == 1) printf("wm: key %#x -> window %u\n", e.code, g_win[g_focus].id);
  }
  forward(g_focus, fe);
}

// Кадр: повреждённое заново в заднем буфере (фон, окна снизу вверх, курсор), на экран.
void compose() {
  Rect d = intersect(g_damage, g_s.all());
  g_damage = {};
  if (d.empty()) return;
  g_s.fill(d, kInk, d);
  for (int i = 0; i < g_zn; ++i) {
    Window& w = g_win[g_z[i]];
    Rect bar{w.frame.x, w.frame.y, w.frame.w, kTitle};
    g_s.fill(bar, g_z[i] == g_focus ? kAmber : kRaised, d);
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
      memcpy(t, s, static_cast<size_t>(c.w) * 4);
    }
  }
  g_s.fill(g_cursor, kWhite, d);
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
