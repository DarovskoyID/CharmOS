// Тестовый клиент протокола окон (docs/abi/wm.md): окно своего цвета с
// квадратом, который движется кадр за кадром по FRAME_DONE (не по таймеру).
// Клавиша меняет цвет; задержка «ввод → пиксель» — от времени события в ядре
// (input.md §3.1) до FRAME_DONE кадра, где цвет уже другой.
//   wdemo <заголовок> <цвет RRGGBB> [ширина высота [задержка_мс]]
// Задержка перед подключением — чтобы порядок окон в тестах был известен.
#include <abi/wm.hpp>
#include <charm/libc.h>
#include <charm/sys.hpp>
#include <charm/window.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>

namespace {

using charm::Handle;
using charm::Status;
namespace wm = abi::wm;
namespace sc = abi::syscall;

uint32_t g_w, g_h;
bool g_bgrx = true;
int g_io_state = 0;

void* io_worker(void*) {
  printf("wdemo: USB I/O started\n");
  int fd = open("/usb-io.bin", O_CREAT | O_TRUNC | O_RDWR, 0644);
  bool ok = fd >= 0;
  uint8_t data[4096], back[4096];
  for (uint32_t page = 0; ok && page < 512; ++page) {
    for (uint32_t i = 0; i < sizeof data; ++i) data[i] = static_cast<uint8_t>(page + i * 17);
    ok = write(fd, data, sizeof data) == sizeof data;
    if (ok && page % 16 == 15) ok = fsync(fd) == 0;
  }
  if (ok) ok = lseek(fd, 0, SEEK_SET) == 0;
  for (uint32_t page = 0; ok && page < 512; ++page) {
    ok = read(fd, back, sizeof back) == sizeof back;
    for (uint32_t i = 0; ok && i < sizeof back; ++i) ok = back[i] == static_cast<uint8_t>(page + i * 17);
  }
  if (fd >= 0) (void)close(fd);
  __atomic_store_n(&g_io_state, ok ? 1 : -1, __ATOMIC_RELEASE);
  return nullptr;
}

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
  charm::gui::Window window;
  Status status = window.create(title, g_w, g_h);
  if (status != Status::Ok) {
    printf("wdemo %s: window refused (%u)\n", title, static_cast<unsigned>(status));
    return 1;
  }
  // A normal window client cannot inject global input via its compositor door.
  if (charm::syscall2(sc::Number::DeviceInput, charm_wm(), 0).status != Status::WrongType) return 1;
  g_bgrx = window.format() == sc::kDisplayBgrx;
  printf("wdemo %s: window %u at %d,%d\n", title, window.id(), window.x(), window.y());
  bool io_mode = argc > 6 && strcmp(argv[6], "io") == 0;
  bool io_key = false, io_reported = false;
  if (io_mode) {
    pthread_t worker;
    if (pthread_create(&worker, nullptr, io_worker, nullptr) != 0) return 1;
    (void)pthread_detach(worker);
  }

  int cur = 0;
  int32_t sx = 0, dir = 4;
  uint64_t frames = 0, key_ns = 0;
  // 0 — нет; 1 — клавиша пришла, следующий кадр её учтёт; 2 — такой кадр отправлен.
  int key_state = 0;
  draw(window.pixels(static_cast<uint32_t>(cur)), color, sx);
  charm::gui::Damage damage{0, 0, static_cast<int32_t>(g_w), static_cast<int32_t>(g_h)};
  if (window.present(static_cast<uint32_t>(cur), damage) != Status::Ok) return 1;
  for (;;) {
    wm::WindowEvent e{};
    status = window.wait(&e);
    if (status == Status::Interrupted) continue;
    if (status != Status::Ok) return 0;  // компоновщик закрыл канал
    if (e.type == wm::kInput) {
      sc::InputEvent ev{};
      while (window.next_input(&ev)) {
        if (ev.type == sc::kInputKey && ev.value == 1) {
          color ^= 0xffffff;
          key_ns = ev.time_ns;
          key_state = 1;
          io_key = true;
          printf("wdemo %s: key %#x\n", title, ev.code);
        }
      }
    } else if (e.type == wm::kCloseRequest) {
      return 0;
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
      draw(window.pixels(static_cast<uint32_t>(cur)), color, sx);
      if (window.present(static_cast<uint32_t>(cur), damage) != Status::Ok) return 1;
      if (key_state == 1) key_state = 2;
      if (io_mode && !io_reported) {
        int state = __atomic_load_n(&g_io_state, __ATOMIC_ACQUIRE);
        if (state < 0) { printf("wdemo: USB I/O failed\n"); return 1; }
        if (state == 1 && io_key) { printf("wdemo: USB I/O and input verified\n"); io_reported = true; }
      }
    }
  }
}
