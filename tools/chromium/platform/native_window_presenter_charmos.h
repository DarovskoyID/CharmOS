#ifndef CHARMOS_NATIVE_WINDOW_PRESENTER_H_
#define CHARMOS_NATIVE_WINDOW_PRESENTER_H_

#include "base/memory/ref_counted_delete_on_sequence.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "build/charmos/window_event_source_charmos.h"
#include "third_party/skia/include/core/SkImage.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/ozone/public/surface_ozone_canvas.h"

namespace ui {
// All methods except ui_runner() and reference operations run on the UI
// sequence. Callbacks supplied to Submit must post to their originating runner.
class NativeWindowPresenterCharmOS final
    : public base::RefCountedDeleteOnSequence<NativeWindowPresenterCharmOS> {
 public:
  using SwapCallback=SurfaceOzoneCanvas::SwapBuffersCallback;
  using EventCallback=WindowEventSourceCharmOS::Callback;
  explicit NativeWindowPresenterCharmOS(scoped_refptr<base::SequencedTaskRunner> ui_runner);
  charm::Status Initialize(const char* title,uint32_t width,uint32_t height,EventCallback observer);
  void Close();
  charm::Status RequestBounds(const gfx::Rect& bounds);
  charm::gui::Window& window();
  void Submit(sk_sp<SkImage> image,const gfx::Rect& damage,SwapCallback callback);
  const scoped_refptr<base::SequencedTaskRunner>& ui_runner() const { return ui_runner_; }

 private:
  friend class base::RefCountedDeleteOnSequence<NativeWindowPresenterCharmOS>;
  friend class base::DeleteHelper<NativeWindowPresenterCharmOS>;
  ~NativeWindowPresenterCharmOS();
  void OnEvent(charm::Status status,const abi::wm::WindowEvent& event);
  void TrySubmit();
  void Complete(const gfx::Size& size);
  SEQUENCE_CHECKER(sequence_checker_);
  const scoped_refptr<base::SequencedTaskRunner> ui_runner_;
  charm::gui::Window window_;
  WindowEventSourceCharmOS events_;
  EventCallback observer_;
  sk_sp<SkImage> image_;
  gfx::Rect damage_;
  SwapCallback completion_;
  gfx::Size submitted_size_;
  uint32_t expected_release_=0;
  bool in_flight_=false;
};
}  // namespace ui
#endif
