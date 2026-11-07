#ifndef CHARMOS_SURFACE_FACTORY_H_
#define CHARMOS_SURFACE_FACTORY_H_

#include "ui/ozone/public/surface_factory_ozone.h"

namespace ui {
class NativeWindowRegistryCharmOS;

class SurfaceFactoryCharmOS final : public SurfaceFactoryOzone {
 public:
  explicit SurfaceFactoryCharmOS(NativeWindowRegistryCharmOS* registry);
  ~SurfaceFactoryCharmOS() override;
  std::unique_ptr<SurfaceOzoneCanvas> CreateCanvasForWidget(
      gfx::AcceleratedWidget widget) override;
  bool CanCreateNativePixmapForFormat(viz::SharedImageFormat format) override;

 private:
  raw_ptr<NativeWindowRegistryCharmOS> registry_;
};
}  // namespace ui
#endif
