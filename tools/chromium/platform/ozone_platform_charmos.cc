#include "build/charmos/ozone_platform_charmos.h"

#include <stdio.h>

#include "base/check.h"
#include "build/charmos/native_window_registry_charmos.h"
#include "build/charmos/platform_screen_charmos.h"
#include "build/charmos/platform_window_charmos.h"
#include "build/charmos/surface_factory_charmos.h"
#include "ui/base/cursor/cursor_factory.h"
#include "ui/base/ime/input_method_minimal.h"
#include "ui/display/types/native_display_delegate.h"
#include "ui/events/ozone/layout/keyboard_layout_engine_manager.h"
#include "ui/events/ozone/layout/stub/stub_keyboard_layout_engine.h"
#include "ui/events/platform/platform_event_source.h"
#include "ui/ozone/common/bitmap_cursor_factory.h"
#include "ui/ozone/common/stub_client_native_pixmap_factory.h"
#include "ui/ozone/common/stub_overlay_manager.h"
#include "ui/ozone/public/gpu_platform_support_host.h"
#include "ui/ozone/public/input_controller.h"
#include "ui/ozone/public/stub_input_controller.h"
#include "ui/ozone/public/system_input_injector.h"

namespace ui {
namespace {
class CharmPlatformEventSource final : public PlatformEventSource {
 public:
  CharmPlatformEventSource() = default;
  ~CharmPlatformEventSource() override = default;
};

class OzonePlatformCharmOSImpl final : public OzonePlatform {
 public:
  OzonePlatformCharmOSImpl() = default;
  ~OzonePlatformCharmOSImpl() override = default;

  SurfaceFactoryOzone* GetSurfaceFactoryOzone() override { return surface_factory_.get(); }
  OverlayManagerOzone* GetOverlayManager() override { return overlay_manager_.get(); }
  CursorFactory* GetCursorFactory() override { return cursor_factory_.get(); }
  InputController* GetInputController() override { return input_controller_.get(); }
  GpuPlatformSupportHost* GetGpuPlatformSupportHost() override {
    return gpu_platform_support_host_.get();
  }
  std::unique_ptr<SystemInputInjector> CreateSystemInputInjector() override {
    return nullptr;
  }
  std::unique_ptr<PlatformWindow> CreatePlatformWindow(
      PlatformWindowDelegate* delegate,PlatformWindowInitProperties properties) override {
    auto window = std::make_unique<PlatformWindowCharmOS>(delegate,registry_.get());
    if (!window->Initialize(properties)) return nullptr;
    return window;
  }
  std::unique_ptr<display::NativeDisplayDelegate> CreateNativeDisplayDelegate() override {
    return nullptr;
  }
  std::unique_ptr<PlatformScreen> CreateScreen() override {
    CHECK(screen_);
    return std::move(screen_);
  }
  void InitScreen(PlatformScreen* screen) override {}
  std::unique_ptr<InputMethod> CreateInputMethod(
      ImeKeyEventDispatcher* dispatcher,gfx::AcceleratedWidget widget) override {
    return std::make_unique<InputMethodMinimal>(dispatcher);
  }
  bool IsWindowCompositingSupported() const override { return true; }
  const PlatformProperties& GetPlatformProperties() override {
    properties_.platform_shows_drag_image = false;
    return properties_;
  }
  const PlatformRuntimeProperties& GetPlatformRuntimeProperties() override {
    runtime_properties_.supports_server_side_window_decorations = true;
    return runtime_properties_;
  }

  bool InitializeUI(const InitParams& params) override {
    registry_ = std::make_unique<NativeWindowRegistryCharmOS>();
    charm::gui::Window probe;
    const charm::Status probe_status = probe.create_managed(
        "Ozone display probe", 1, 1,
        abi::wm::kCreateInvisible | abi::wm::kCreateInactive);
    if (probe_status != charm::Status::Ok) {
      fprintf(stderr, "[content_shell] WM probe failed: %d\n",
              static_cast<int>(probe_status));
      return false;
    }
    const auto& native = probe.display();
    if (!native.width || !native.height || !native.work.width || !native.work.height) {
      fprintf(stderr, "[content_shell] WM probe size %ux%u work %ux%u\n",
              native.width, native.height, native.work.width, native.work.height);
      probe.close();
      return false;
    }
    const gfx::Rect bounds(0,0,static_cast<int>(native.width),static_cast<int>(native.height));
    const gfx::Rect work(native.work.x,native.work.y,
                         static_cast<int>(native.work.width),static_cast<int>(native.work.height));
    probe.close();
    surface_factory_ = std::make_unique<SurfaceFactoryCharmOS>(registry_.get());
    screen_ = std::make_unique<PlatformScreenCharmOS>(registry_.get(),bounds,work);
    if (!PlatformEventSource::GetInstance())
      event_source_ = std::make_unique<CharmPlatformEventSource>();
    keyboard_layout_engine_ = std::make_unique<StubKeyboardLayoutEngine>();
    KeyboardLayoutEngineManager::SetKeyboardLayoutEngine(keyboard_layout_engine_.get());
    overlay_manager_ = std::make_unique<StubOverlayManager>();
    input_controller_ = std::make_unique<StubInputController>();
    cursor_factory_ = std::make_unique<BitmapCursorFactory>();
    gpu_platform_support_host_.reset(CreateStubGpuPlatformSupportHost());
    return true;
  }
  void InitializeGPU(const InitParams& params) override {
    CHECK(has_initialized_ui()) << "A separate GPU process is not supported";
  }

 private:
  std::unique_ptr<KeyboardLayoutEngine> keyboard_layout_engine_;
  std::unique_ptr<NativeWindowRegistryCharmOS> registry_;
  std::unique_ptr<SurfaceFactoryCharmOS> surface_factory_;
  std::unique_ptr<PlatformScreen> screen_;
  std::unique_ptr<PlatformEventSource> event_source_;
  std::unique_ptr<CursorFactory> cursor_factory_;
  std::unique_ptr<InputController> input_controller_;
  std::unique_ptr<GpuPlatformSupportHost> gpu_platform_support_host_;
  std::unique_ptr<OverlayManagerOzone> overlay_manager_;
  PlatformProperties properties_;
  PlatformRuntimeProperties runtime_properties_;
};
}  // namespace

OzonePlatform* CreateOzonePlatformCharmos() { return new OzonePlatformCharmOSImpl; }
gfx::ClientNativePixmapFactory* CreateClientNativePixmapFactoryCharmos() {
  return CreateStubClientNativePixmapFactory();
}
}  // namespace ui
