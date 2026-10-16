// Тестовый клиент протокола окон (docs/abi/wm.md): окно своего цвета с
// квадратом, который движется кадр за кадром по FRAME_DONE (не по таймеру).
// Клавиша меняет цвет; задержка «ввод → пиксель» — от времени события в ядре
// (input.md §3.1) до FRAME_DONE кадра, где цвет уже другой.
//   wdemo <заголовок> <цвет RRGGBB> [ширина высота [задержка_мс]]
// Задержка перед подключением — чтобы порядок окон в тестах был известен.
#include <abi/wm.hpp>
#include <charm/libc.h>
#include <charm/sys.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace {

using charm::Handle;
using charm::Status;
namespace wm = abi::wm;
namespace sc = abi::syscall;

uint32_t g_w, g_h;
bool g_bgrx = true;

uint32_t pixel(uint32_t rgb) { return g_bgrx ? rgb : ((rgb & 0xff) << 16 | (rgb & 0xff00) | (rgb >> 16 & 0xff)); }

void draw(uint32_t* b, uint32_t bg, int32_t sx) {
  uint32_t p = pixel(bg), q = pixel(0xffffff);
  for (uint32_t y = 0; y < g_h; ++y) {
    for (uint32_t x = 0; x < g_w; ++x) {
      bool sq = static_cast<int32_t>(x) >= sx && static_cast<int32_t>(x) < sx + 20 && y >= g_h / 2 - 10 && y < g_h / 2 + 10;
      b[y * g_w + x] = sq ? q : p;
    }
  }
}

}  // namespace

extern "C" int main(int argc, char** argv) {
  const char* title = argc > 1 ? argv[1] : "demo";
  uint32_t color = argc > 2 ? static_cast<uint32_t>(strtoul(argv[2], nullptr, 16)) : 0x336699;
  g_w = argc > 4 ? static_cast<uint32_t>(atoi(argv[3])) : 280;
  g_h = argc > 4 ? static_cast<uint32_t>(atoi(argv[4])) : 180;
  if (argc > 5) usleep(static_cast<unsigned>(atoi(argv[5])) * 1000u);
  Handle door = charm_wm();
  if (door == 0) {
    printf("wdemo %s: no compositor door\n", title);
    return 1;
  }
  Handle ch[2];
  if (charm::channel_create(ch) != Status::Ok) return 1;
  wm::Helo helo{wm::kHelo, wm::kVersion};
  if (charm::channel_write(door, &helo, sizeof helo, &ch[1], 1) != Status::Ok) return 1;
  wm::Welcome welcome{};
  Handle none[1];
  auto r = charm::channel_read(ch[0], &welcome, sizeof welcome, none, 1, charm::kForever);
  if (!r.ok() || welcome.type != wm::kWelcome || welcome.version != wm::kVersion) return 1;
  g_bgrx = welcome.format == sc::kDisplayBgrx;

  uint64_t bytes = (uint64_t{g_w} * g_h * 4 + 4095) / 4096 * 4096;
  Handle bufs[2];
  uint32_t* mem[2];
  for (int i = 0; i < 2; ++i) {
    auto v = charm::vmo_create(bytes);
    if (!v.ok()) return 1;
    auto m = charm::vmo_map(static_cast<Handle>(v.value), charm::rights::kRead | charm::rights::kWrite);
    if (!m.ok()) return 1;
    mem[i] = reinterpret_cast<uint32_t*>(m.value);
    auto d = charm::handle_duplicate(static_cast<Handle>(v.value), charm::rights::kRead | charm::rights::kMap |
                                                                       charm::rights::kTransfer);
    if (!d.ok()) return 1;
    bufs[i] = static_cast<Handle>(d.value);
  }
  wm::Create cr{wm::kCreate, g_w, g_h, {}};
  strncpy(cr.title, title, sizeof cr.title - 1);
  if (charm::channel_write(ch[0], &cr, sizeof cr, bufs, 2) != Status::Ok) return 1;
  wm::Created created{};
  Handle ring_h[1];
  do {  // до CREATED могут прийти другие события — пропускаем
    r = charm::channel_read(ch[0], &created, sizeof created, ring_h, 1, charm::kForever);
  } while (r.ok() && created.type != wm::kCreated);
  if (!r.ok() || created.window == 0 || (r.value & 0xffffffff) != 1) {
    printf("wdemo %s: window refused\n", title);
    return 1;
  }
  auto rm = charm::vmo_map(ring_h[0], charm::rights::kRead | charm::rights::kWrite);
  if (!rm.ok()) return 1;
  auto* ring = reinterpret_cast<sc::InputRingHeader*>(rm.value);
  auto* events = reinterpret_cast<sc::InputEvent*>(rm.value + sizeof *ring);
  printf("wdemo %s: window %u at %d,%d\n", title, created.window, created.x, created.y);

  int cur = 0;
  int32_t sx = 0, dir = 4;
  uint64_t frames = 0, key_ns = 0;
  // 0 — нет; 1 — клавиша пришла, следующий кадр её учтёт; 2 — такой кадр отправлен.
  int key_state = 0;
  draw(mem[cur], color, sx);
  wm::Present pr{wm::kPresent, created.window, static_cast<uint32_t>(cur), 0, 0, static_cast<int32_t>(g_w),
                 static_cast<int32_t>(g_h)};
  (void)charm::channel_write(ch[0], &pr, sizeof pr, nullptr, 0);
  for (;;) {
    wm::WindowEvent e{};
    r = charm::channel_read(ch[0], &e, sizeof e, none, 1, charm::kForever);
    if (r.status == Status::Interrupted) continue;
    if (!r.ok()) return 0;  // компоновщик закрыл канал
    if (e.type == wm::kInput) {
      uint32_t tail = __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE);
      for (uint32_t head = ring->head; head != tail; ++head) {
        const sc::InputEvent& ev = events[head % ring->capacity];
        if (ev.type == sc::kInputKey && ev.value == 1) {
          color ^= 0xffffff;  // клавиша — цвет наоборот
          key_ns = ev.time_ns;
          key_state = 1;
          printf("wdemo %s: key %#x\n", title, ev.code);
        }
      }
      __atomic_store_n(&ring->head, tail, __ATOMIC_RELEASE);
    } else if (e.type == wm::kFrameDone) {
      if (key_state == 2) {
        // FRAME_DONE после кадра, нарисованного уже с новым цветом: он выведен на экран.
        printf("wdemo %s: input->pixel %llu us\n", title,
               static_cast<unsigned long long>((charm::now_ns() - key_ns) / 1000));
        key_state = 0;
      }
      ++frames;
      if (frames % 60 == 0) printf("wdemo %s: %llu frames\n", title, static_cast<unsigned long long>(frames));
      cur = static_cast<int>(e.value);  // этот буфер снова наш
      sx += dir;
      if (sx < 0 || sx + 20 > static_cast<int32_t>(g_w)) {
        dir = -dir;
        sx += 2 * dir;
      }
      draw(mem[cur], color, sx);
      pr.buffer = static_cast<uint32_t>(cur);
      (void)charm::channel_write(ch[0], &pr, sizeof pr, nullptr, 0);
      if (key_state == 1) key_state = 2;
    }
  }
}
