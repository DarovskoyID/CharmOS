// Компоновщик (M10, os-gui-design.md фазы 4 и 6): экран — хендл Display
// (docs/abi/display.md), который init даёт только ему. Рисует в задний буфер
// в обычной памяти и копирует во фреймбуфер только повреждённые
// прямоугольники (damage tracking): запись во фреймбуфер — узкое место.
// Сейчас — шаг фазы 4: фон и движущийся прямоугольник, замер копирования.
#include <abi/process.hpp>
#include <charm/libc.h>
#include <charm/sys.hpp>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace {

using charm::Handle;
using charm::Status;
using abi::syscall::Number;

// Токены UI-кита (os-gui-design.md, «Визуальный дизайн»), 0xRRGGBB.
constexpr uint32_t kInk = 0x0b0b0c;
constexpr uint32_t kAmber = 0xf5c518;

struct Rect {
  int32_t x, y, w, h;
  bool empty() const { return w <= 0 || h <= 0; }
};

Rect unite(Rect a, Rect b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  int32_t x0 = a.x < b.x ? a.x : b.x, y0 = a.y < b.y ? a.y : b.y;
  int32_t x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
  int32_t y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
  return {x0, y0, x1 - x0, y1 - y0};
}

struct Screen {
  uint32_t width, height, pitch;  // pitch — в пикселях
  bool bgrx;
  volatile uint32_t* fb;  // write-combining: только запись
  uint32_t* back;         // задний буфер, тот же формат, pitch = width
  uint64_t copied = 0;    // байт за всё время

  uint32_t pixel(uint32_t rgb) const {
    // B,G,R,X по байтам = 0xXXRRGGBB в little-endian слове; R,G,B,X — наоборот.
    return bgrx ? rgb : ((rgb & 0xff) << 16 | (rgb & 0xff00) | (rgb >> 16 & 0xff));
  }
  Rect clip(Rect r) const {
    int32_t x0 = r.x < 0 ? 0 : r.x, y0 = r.y < 0 ? 0 : r.y;
    int32_t x1 = r.x + r.w, y1 = r.y + r.h;
    if (x1 > static_cast<int32_t>(width)) x1 = static_cast<int32_t>(width);
    if (y1 > static_cast<int32_t>(height)) y1 = static_cast<int32_t>(height);
    return {x0, y0, x1 - x0, y1 - y0};
  }
  void fill(Rect r, uint32_t rgb) {
    r = clip(r);
    if (r.empty()) return;
    uint32_t p = pixel(rgb);
    for (int32_t y = r.y; y < r.y + r.h; ++y) {
      uint32_t* row = back + static_cast<uint64_t>(y) * width;
      for (int32_t x = r.x; x < r.x + r.w; ++x) row[x] = p;
    }
  }
  // Повреждённое — на экран (построчно, слово за словом: WC-память собирает записи).
  void present(Rect r) {
    r = clip(r);
    if (r.empty()) return;
    for (int32_t y = r.y; y < r.y + r.h; ++y) {
      const uint32_t* src = back + static_cast<uint64_t>(y) * width + r.x;
      volatile uint32_t* dst = fb + static_cast<uint64_t>(y) * pitch + r.x;
      for (int32_t x = 0; x < r.w; ++x) dst[x] = src[x];
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);  // WC-буферы — в память до следующего шага
    copied += static_cast<uint64_t>(r.w) * static_cast<uint64_t>(r.h) * 4;
  }
};

}  // namespace

extern "C" int main(int, char**) {
  Handle display = charm_display();
  if (display == 0) {
    printf("wm: no display handle\n");
    return 1;
  }
  abi::syscall::DisplayInfo info{};
  if (charm::syscall2(Number::DisplayInfo, display, reinterpret_cast<uint64_t>(&info)).status != Status::Ok) {
    printf("wm: display_info failed (unsupported pixel format?)\n");
    return 1;
  }
  auto vmo = charm::syscall1(Number::DisplayMap, display);
  if (!vmo.ok()) {
    printf("wm: display_map failed\n");
    return 1;
  }
  auto fb = charm::vmo_map(static_cast<Handle>(vmo.value), charm::rights::kRead | charm::rights::kWrite);
  auto back_vmo = charm::vmo_create((uint64_t{info.width} * info.height * 4 + 4095) / 4096 * 4096);
  if (!fb.ok() || !back_vmo.ok()) {
    printf("wm: cannot map framebuffer\n");
    return 1;
  }
  auto back = charm::vmo_map(static_cast<Handle>(back_vmo.value), charm::rights::kRead | charm::rights::kWrite);
  if (!back.ok()) return 1;
  Screen s{info.width, info.height, info.pitch / 4, info.format == abi::syscall::kDisplayBgrx,
           reinterpret_cast<volatile uint32_t*>(fb.value), reinterpret_cast<uint32_t*>(back.value)};
  printf("wm: display %ux%u, pitch %u, %s\n", info.width, info.height, info.pitch, s.bgrx ? "BGRX" : "RGBX");

  // Первый кадр — весь экран.
  Rect all{0, 0, static_cast<int32_t>(info.width), static_cast<int32_t>(info.height)};
  uint64_t t0 = charm::now_ns();
  s.fill(all, kInk);
  s.present(all);
  uint64_t full_us = (charm::now_ns() - t0) / 1000;

  // Кадры с движением: повреждено только старое и новое место прямоугольника.
  Rect box{100, 100, 200, 120};
  s.fill(box, kAmber);
  s.present(box);
  constexpr int kFrames = 30;
  uint64_t before = s.copied;
  t0 = charm::now_ns();
  for (int i = 0; i < kFrames; ++i) {
    Rect old = box;
    box.x += 4;
    s.fill(old, kInk);
    s.fill(box, kAmber);
    s.present(unite(old, box));
  }
  uint64_t damage_us = (charm::now_ns() - t0) / 1000;
  printf("wm: full frame %llu us; %d damage frames %llu us, %llu KiB copied (full frame %llu KiB)\n",
         static_cast<unsigned long long>(full_us), kFrames, static_cast<unsigned long long>(damage_us),
         static_cast<unsigned long long>((s.copied - before) / 1024),
         static_cast<unsigned long long>(uint64_t{info.width} * info.height * 4 / 1024));
  printf("wm: frame ready, box at %d,%d\n", box.x, box.y);

  // Ввод (docs/abi/input.md): своё кольцо, ядро пишет в него события PS/2.
  Handle input = charm_input();
  if (input == 0) {
    printf("wm: no input handle\n");
    for (;;) sleep(1);
  }
  auto ring_vmo = charm::vmo_create(4096);
  if (!ring_vmo.ok()) return 1;
  auto ring_map = charm::vmo_map(static_cast<Handle>(ring_vmo.value), charm::rights::kRead | charm::rights::kWrite);
  if (!ring_map.ok()) return 1;
  auto* ring = reinterpret_cast<abi::syscall::InputRingHeader*>(ring_map.value);
  auto* events = reinterpret_cast<abi::syscall::InputEvent*>(ring_map.value + sizeof *ring);
  memcpy(ring->magic, "INPR", 4);
  ring->version = 1;
  ring->capacity = 128;  // 64 + 128 × 24 ≤ 4096
  if (charm::syscall2(Number::InputAttach, input, static_cast<Handle>(ring_vmo.value)).status != Status::Ok) {
    printf("wm: input_attach failed\n");
    return 1;
  }
  auto port = charm::port_create();
  if (!port.ok()) return 1;
  Handle port_h = static_cast<Handle>(port.value);
  // Курсор — квадрат 8×8, движется по событиям мыши (повреждено старое и новое место).
  Rect cursor{static_cast<int32_t>(info.width / 2), static_cast<int32_t>(info.height / 2), 8, 8};
  s.fill(cursor, 0xffffff);
  s.present(cursor);
  printf("wm: input ready\n");
  for (;;) {
    (void)charm::object_bind(port_h, input, 1);
    charm::PortPacket pk;
    (void)charm::port_wait(port_h, &pk, charm::kForever);
    uint32_t tail = __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE);
    Rect damage{};
    for (uint32_t head = ring->head; head != tail; ++head) {
      const abi::syscall::InputEvent& e = events[head % ring->capacity];
      if (e.type == abi::syscall::kInputKey) {
        printf("wm: key %#x %s\n", e.code, e.value != 0 ? "down" : "up");
      } else if (e.type == abi::syscall::kInputButton) {
        printf("wm: button %u %s\n", e.code, e.value != 0 ? "down" : "up");
      } else if (e.type == abi::syscall::kInputMotion) {
        Rect old = cursor;
        cursor.x += e.x;
        cursor.y += e.y;
        if (cursor.x < 0) cursor.x = 0;
        if (cursor.y < 0) cursor.y = 0;
        if (cursor.x > static_cast<int32_t>(info.width) - 8) cursor.x = static_cast<int32_t>(info.width) - 8;
        if (cursor.y > static_cast<int32_t>(info.height) - 8) cursor.y = static_cast<int32_t>(info.height) - 8;
        s.fill(old, kInk);
        damage = unite(damage, unite(old, cursor));
      }
    }
    __atomic_store_n(&ring->head, tail, __ATOMIC_RELEASE);
    if (!damage.empty()) {
      s.fill(cursor, 0xffffff);
      s.present(damage);
      printf("wm: cursor %d,%d\n", cursor.x, cursor.y);
    }
  }
}
