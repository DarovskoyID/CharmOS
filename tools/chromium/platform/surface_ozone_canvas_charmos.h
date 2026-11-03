#ifndef CHARMOS_SURFACE_OZONE_CANVAS_H_
#define CHARMOS_SURFACE_OZONE_CANVAS_H_

#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "build/charmos/native_window_presenter_charmos.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "ui/ozone/public/surface_ozone_canvas.h"

namespace ui {
class SurfaceOzoneCanvasCharmOS final : public SurfaceOzoneCanvas {
 public:
  explicit SurfaceOzoneCanvasCharmOS(scoped_refptr<NativeWindowPresenterCharmOS> presenter);
  ~SurfaceOzoneCanvasCharmOS() override;
  SkCanvas* GetCanvas() override;
  void ResizeCanvas(const gfx::Size& viewport_size,float scale) override;
  void PresentCanvas(const gfx::Rect& damage) override;
  std::unique_ptr<gfx::VSyncProvider> CreateVSyncProvider() override;
  bool SupportsAsyncBufferSwap() const override;
  void OnSwapBuffers(SwapBuffersCallback callback,gfx::FrameData data) override;
  int MaxFramesPending() const override;

 private:
  void DidSwap(SwapBuffersCallback callback,const gfx::Size& size);
  SEQUENCE_CHECKER(sequence_checker_);
  scoped_refptr<NativeWindowPresenterCharmOS> presenter_;
  sk_sp<SkSurface> surface_;
  sk_sp<SkImage> snapshot_;
  gfx::Rect damage_;
  bool swap_pending_=false;
  base::WeakPtrFactory<SurfaceOzoneCanvasCharmOS> weak_factory_{this};
};
}  // namespace ui
#endif
