#include "build/charmos/native_window_presenter_charmos.h"

#include "base/check.h"
#include "base/functional/bind.h"
#include "third_party/skia/include/core/SkPixmap.h"

namespace ui {
NativeWindowPresenterCharmOS::NativeWindowPresenterCharmOS(
    scoped_refptr<base::SequencedTaskRunner> ui_runner)
    : base::RefCountedDeleteOnSequence<NativeWindowPresenterCharmOS>(ui_runner),
      ui_runner_(std::move(ui_runner)) {
  CHECK(ui_runner_->RunsTasksInCurrentSequence());
}
NativeWindowPresenterCharmOS::~NativeWindowPresenterCharmOS() { Close(); }
charm::Status NativeWindowPresenterCharmOS::Initialize(
    const char* title,uint32_t width,uint32_t height,EventCallback observer,uint32_t flags) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if(window_.id()) return charm::Status::InvalidArgs;
  auto status=window_.create_managed(title,width,height,flags);
  if(status!=charm::Status::Ok) return status;
  observer_=std::move(observer);
  if(!events_.Start(&window_,base::BindRepeating(&NativeWindowPresenterCharmOS::OnEvent,
                                               base::Unretained(this)))) {
    Close(); return charm::Status::NoMemory;
  }
  return charm::Status::Ok;
}
void NativeWindowPresenterCharmOS::Close() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  events_.Stop();
  window_.close();
  observer_.Reset();
  Complete(gfx::Size());
}
charm::gui::Window& NativeWindowPresenterCharmOS::window() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return window_;
}
charm::Status NativeWindowPresenterCharmOS::RequestBounds(const gfx::Rect& bounds) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if(bounds.IsEmpty()) return charm::Status::InvalidArgs;
  auto status=window_.request_bounds(bounds.x(),bounds.y(),bounds.width(),bounds.height());
  if(status==charm::Status::Ok && window_.configuration_pending()) Complete(gfx::Size());
  return status;
}
void NativeWindowPresenterCharmOS::Submit(sk_sp<SkImage> image,const gfx::Rect& damage,
                                         SwapCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if(completion_ || !window_.id() || !image || damage.IsEmpty() ||
      !gfx::Rect(image->width(),image->height()).Contains(damage)) {
    std::move(callback).Run(gfx::Size());
    return;
  }
  image_=std::move(image); damage_=damage; completion_=std::move(callback);
  TrySubmit();
}
void NativeWindowPresenterCharmOS::TrySubmit() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if(!completion_ || in_flight_ || window_.configuration_pending()) return;
  if(!window_.id() || image_->width()!=static_cast<int>(window_.width()) ||
     image_->height()!=static_cast<int>(window_.height())) {
    Complete(gfx::Size()); return;
  }
  SkPixmap pixels;
  if(!image_->peekPixels(&pixels) || pixels.colorType()!=kBGRA_8888_SkColorType ||
     pixels.rowBytes()>UINT32_MAX) { Complete(gfx::Size()); return; }
  uint32_t submitted=0;
  auto status=window_.present_pixels(pixels.addr(),pixels.computeByteSize(),
      static_cast<uint32_t>(pixels.rowBytes()),abi::syscall::kDisplayBgrx,
      {damage_.x(),damage_.y(),damage_.width(),damage_.height()},&submitted);
  if(status==charm::Status::WouldBlock) return;
  if(status!=charm::Status::Ok) { Complete(gfx::Size()); return; }
  submitted_size_=gfx::Size(image_->width(),image_->height());
  image_.reset(); in_flight_=true; expected_release_=1-submitted;
}
void NativeWindowPresenterCharmOS::Complete(const gfx::Size& size) {
  const gfx::Size result=size;
  in_flight_=false;
  image_.reset();
  submitted_size_=gfx::Size();
  auto callback=std::move(completion_);
  if(callback) std::move(callback).Run(result);
}
void NativeWindowPresenterCharmOS::OnEvent(charm::Status status,const abi::wm::WindowEvent& event) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // An observer may release its last external owner or close the window.
  scoped_refptr<NativeWindowPresenterCharmOS> keep_alive(this);
  if(status!=charm::Status::Ok) {
    auto observer=observer_;
    Close();
    if(observer) observer.Run(status,event);
    return;
  }
  if(event.type==abi::wm::kFrameDone && in_flight_) {
    if(event.value!=expected_release_) {
      auto observer=observer_;
      Close();
      if(observer) observer.Run(charm::Status::InvalidArgs,event);
      return;
    }
    const gfx::Size size=submitted_size_;
    Complete(size);
  }
  if(event.type==abi::wm::kFrameDone || event.type==abi::wm::kConfigured) TrySubmit();
  auto observer=observer_;
  if(observer) observer.Run(status,event);
}
}  // namespace ui
