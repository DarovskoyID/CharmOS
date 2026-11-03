#include "build/charmos/surface_ozone_canvas_charmos.h"

#include <cmath>
#include "base/functional/bind.h"
#include "base/task/bind_post_task.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "ui/gfx/vsync_provider.h"

namespace ui {
SurfaceOzoneCanvasCharmOS::SurfaceOzoneCanvasCharmOS(
    scoped_refptr<NativeWindowPresenterCharmOS> presenter) : presenter_(std::move(presenter)) {}
SurfaceOzoneCanvasCharmOS::~SurfaceOzoneCanvasCharmOS() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}
SkCanvas* SurfaceOzoneCanvasCharmOS::GetCanvas() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return surface_ ? surface_->getCanvas() : nullptr;
}
void SurfaceOzoneCanvasCharmOS::ResizeCanvas(const gfx::Size& size,float scale) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  surface_.reset(); snapshot_.reset();
  if(size.IsEmpty() || size.width()>16384 || size.height()>16384 || !std::isfinite(scale) || scale<=0) return;
  surface_=SkSurfaces::Raster(SkImageInfo::Make(size.width(),size.height(),
                                              kBGRA_8888_SkColorType,kOpaque_SkAlphaType));
  if(surface_) surface_->getCanvas()->clear(SK_ColorBLACK);
}
void SurfaceOzoneCanvasCharmOS::PresentCanvas(const gfx::Rect& damage) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  snapshot_.reset();
  if(!surface_ || damage.IsEmpty() || !gfx::Rect(surface_->width(),surface_->height()).Contains(damage)) return;
  snapshot_=surface_->makeImageSnapshot(); damage_=damage;
}
std::unique_ptr<gfx::VSyncProvider> SurfaceOzoneCanvasCharmOS::CreateVSyncProvider() { return nullptr; }
bool SurfaceOzoneCanvasCharmOS::SupportsAsyncBufferSwap() const { return true; }
int SurfaceOzoneCanvasCharmOS::MaxFramesPending() const { return 1; }
void SurfaceOzoneCanvasCharmOS::OnSwapBuffers(SwapBuffersCallback callback,gfx::FrameData) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if(swap_pending_ || !snapshot_) {
    std::move(callback).Run(gfx::Size()); return;
  }
  swap_pending_=true;
  auto reply=base::BindPostTaskToCurrentDefault(base::BindOnce(&SurfaceOzoneCanvasCharmOS::DidSwap,
      weak_factory_.GetWeakPtr(),std::move(callback)));
  auto submit=base::BindOnce(&NativeWindowPresenterCharmOS::Submit,presenter_,
                             std::move(snapshot_),damage_,std::move(reply));
  presenter_->ui_runner()->PostTask(FROM_HERE,std::move(submit));
}
void SurfaceOzoneCanvasCharmOS::DidSwap(SwapBuffersCallback callback,const gfx::Size& size) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  swap_pending_=false;
  std::move(callback).Run(size);
}
}  // namespace ui
