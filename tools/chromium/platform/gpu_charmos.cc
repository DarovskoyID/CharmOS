// CharmOS graphics platform queries.
//
// Rendering is software-only: ANGLE's EGL backend can still create an
// offscreen context, but there is no native window surface or GPU process
// presentation path. These entry points report that accurately so Chromium's
// software compositor is selected.
#include "gpu/config/gpu_info_collector.h"
#include "gpu/ipc/service/image_transport_surface.h"

namespace gpu {

bool CollectBasicGraphicsInfo(GPUInfo* gpu_info) {
  gpu_info->gl_vendor = "CharmOS";
  gpu_info->gl_renderer = "CharmOS software rasterizer";
  gpu_info->gl_version = "OpenGL ES 3.0 (ANGLE)";
  return true;
}

bool CollectContextGraphicsInfo(GPUInfo* gpu_info) {
  return CollectBasicGraphicsInfo(gpu_info);
}

scoped_refptr<gl::Presenter> ImageTransportSurface::CreatePresenter(
    scoped_refptr<SharedContextState> /*context_state*/,
    const GpuDriverBugWorkarounds& /*workarounds*/,
    const GpuFeatureInfo& /*gpu_feature_info*/,
    SurfaceHandle /*surface_handle*/) {
  return nullptr;
}

scoped_refptr<gl::GLSurface> ImageTransportSurface::CreateNativeGLSurface(
    gl::GLDisplay* /*display*/,
    SurfaceHandle /*surface_handle*/,
    gl::GLSurfaceFormat /*format*/) {
  return nullptr;
}

}  // namespace gpu
