#pragma once
// Протокол окон клиент ↔ компоновщик (docs/abi/wm.md v2). Сообщения каналов;
// ядро их не разбирает.

#include <stdint.h>

namespace abi::wm {

inline constexpr uint32_t kVersion = 2;
inline constexpr uint32_t kManagedVersion = 3;
inline constexpr uint32_t kFeatureTitle=1, kFeatureBounds=2, kFeatureVisibility=4,
                          kFeatureActivation=8, kFeatureCapture=16;
inline constexpr uint32_t kCreateInvisible=1, kCreateInactive=2;

// §2.1 клиент → компоновщик
inline constexpr uint32_t kHelo = 1, kCreate = 2, kPresent = 3, kDestroy = 4;
inline constexpr uint32_t kConfigure=5, kSetTitle=6, kSetPosition=7,
                          kSetVisibility=8, kActivate=9, kSetCapture=10;
// §2.2 компоновщик → клиент
inline constexpr uint32_t kWelcome = 101, kCreated = 102, kFrameDone = 103, kFocus = 104, kInput = 105,
                          kCloseRequest = 106;
inline constexpr uint32_t kConfigured=107, kBoundsChanged=108, kVisibilityChanged=109,
                          kCaptureChanged=110;

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

struct WelcomeV3 {
  Welcome base;
  uint32_t title_height;
  int32_t work_x,work_y;
  uint32_t work_width,work_height,features;
};
struct CreateV3 { Create base; uint32_t flags; };
struct PresentV3 { Present base; uint32_t generation; };
struct WindowEventV3 {
  WindowEvent base;
  uint32_t generation;
  int32_t x,y;
  uint32_t width,height;
};
struct Configure {
  uint32_t type,window,generation,width,height;
  int32_t x,y;
  uint32_t reserved;
};
struct SetTitle { uint32_t type,window; char title[64]; };
struct SetPosition { uint32_t type,window; int32_t x,y; };
struct SetVisibility { uint32_t type,window,visible,activate; };
struct Activate { uint32_t type,window,active; };
struct SetCapture { uint32_t type,window,captured; };

static_assert(sizeof(Helo) == 8 && sizeof(Create) == 76 && sizeof(Present) == 28);
static_assert(sizeof(Destroy) == 8 && sizeof(Welcome) == 20 && sizeof(Created) == 16 && sizeof(WindowEvent) == 12);
static_assert(__builtin_offsetof(Create, title) == 12 && __builtin_offsetof(Present, x) == 12);
static_assert(sizeof(WelcomeV3)==44 && __builtin_offsetof(WelcomeV3,features)==40);
static_assert(sizeof(CreateV3)==80 && __builtin_offsetof(CreateV3,flags)==76);
static_assert(sizeof(PresentV3)==32 && __builtin_offsetof(PresentV3,generation)==28);
static_assert(sizeof(WindowEventV3)==32 && __builtin_offsetof(WindowEventV3,generation)==12);
static_assert(__builtin_offsetof(WindowEventV3,x)==16 && __builtin_offsetof(WindowEventV3,width)==24);
static_assert(sizeof(Configure)==32 && __builtin_offsetof(Configure,x)==20);
static_assert(sizeof(SetTitle)==72 && sizeof(SetPosition)==16);
static_assert(sizeof(SetVisibility)==16 && sizeof(Activate)==12 && sizeof(SetCapture)==12);

}  // namespace abi::wm
