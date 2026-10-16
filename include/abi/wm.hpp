#pragma once
// Протокол окон клиент ↔ компоновщик (docs/abi/wm.md v2). Сообщения каналов;
// ядро их не разбирает.

#include <stdint.h>

namespace abi::wm {

inline constexpr uint32_t kVersion = 2;

// §2.1 клиент → компоновщик
inline constexpr uint32_t kHelo = 1, kCreate = 2, kPresent = 3, kDestroy = 4;
// §2.2 компоновщик → клиент
inline constexpr uint32_t kWelcome = 101, kCreated = 102, kFrameDone = 103, kFocus = 104, kInput = 105,
                          kCloseRequest = 106;

struct Helo {
  uint32_t type;
  uint32_t version;
};
struct Create {
  uint32_t type;
  uint32_t width, height;
  char title[64];
};
struct Present {
  uint32_t type;
  uint32_t window, buffer;
  int32_t x, y, w, h;
};
struct Destroy {
  uint32_t type;
  uint32_t window;
};
struct Welcome {
  uint32_t type;
  uint32_t version, format, screen_w, screen_h;
};
struct Created {
  uint32_t type;
  uint32_t window;
  int32_t x, y;
};
struct WindowEvent {  // FRAME_DONE (value = освобождённый буфер), FOCUS (value = 1/0), INPUT, CLOSE_REQUEST
  uint32_t type;
  uint32_t window;
  uint32_t value;
};

static_assert(sizeof(Helo) == 8 && sizeof(Create) == 76 && sizeof(Present) == 28);
static_assert(sizeof(Destroy) == 8 && sizeof(Welcome) == 20 && sizeof(Created) == 16 && sizeof(WindowEvent) == 12);
static_assert(__builtin_offsetof(Create, title) == 12 && __builtin_offsetof(Present, x) == 12);

}  // namespace abi::wm
