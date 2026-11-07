#include "build/charmos/surface_factory_charmos.h"

#include "build/charmos/native_window_registry_charmos.h"
#include "build/charmos/platform_window_charmos.h"
#include "build/charmos/surface_ozone_canvas_charmos.h"

namespace ui {
SurfaceFactoryCharmOS::SurfaceFactoryCharmOS(NativeWindowRegistryCharmOS* registry)
    : registry_(registry) {}
SurfaceFactoryCharmOS::~SurfaceFactoryCharmOS() = default;
std::unique_ptr<SurfaceOzoneCanvas> SurfaceFactoryCharmOS::CreateCanvasForWidget(
    gfx::AcceleratedWidget widget) {
  auto presenter=registry_->AcquirePresenter(widget);
  if(!presenter) return nullptr;
  return std::make_unique<SurfaceOzoneCanvasCharmOS>(std::move(presenter));
}
bool SurfaceFactoryCharmOS::CanCreateNativePixmapForFormat(viz::SharedImageFormat) { return false; }
}  // namespace ui
