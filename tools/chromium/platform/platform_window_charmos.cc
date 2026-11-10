#include "build/charmos/platform_window_charmos.h"
#include <algorithm>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "build/charmos/native_window_registry_charmos.h"
#include "ui/base/cursor/platform_cursor.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/events/event.h"

namespace ui {
PlatformWindowCharmOS::PlatformWindowCharmOS(
    PlatformWindowDelegate* delegate,NativeWindowRegistryCharmOS* registry)
    : delegate_(delegate),registry_(registry) {}
PlatformWindowCharmOS::~PlatformWindowCharmOS() {
  weak_factory_.InvalidateWeakPtrs();
  UnregisterWidget();
  if (presenter_) presenter_->Close();
}
bool PlatformWindowCharmOS::Initialize(const PlatformWindowInitProperties& properties) {
  if(properties.type!=PlatformWindowType::kWindow || properties.remove_standard_frame ||
     properties.opacity==PlatformWindowOpacity::kTranslucentWindow) {
    LOG(ERROR)<<"Requested window needs a WM capability not yet available";
    return false;
  }
  activatable_=properties.activatable;
  uint32_t width = properties.bounds.width() > 0 ?
      static_cast<uint32_t>(properties.bounds.width()) : 800;
  uint32_t height = properties.bounds.height() > 0 ?
      static_cast<uint32_t>(properties.bounds.height()) : 600;
  presenter_ = base::MakeRefCounted<NativeWindowPresenterCharmOS>(
      base::SequencedTaskRunner::GetCurrentDefault());
  widget_ = registry_->Add(this);
  uint32_t flags = abi::wm::kCreateInvisible |
      (properties.activatable ? 0u : abi::wm::kCreateInactive);
  auto status = presenter_->Initialize("Chromium",width,height,
      base::BindRepeating(&PlatformWindowCharmOS::OnNativeEvent,
                          weak_factory_.GetWeakPtr()),flags);
  if (status != charm::Status::Ok) {
    registry_->Remove(widget_,this);
    widget_ = gfx::kNullAcceleratedWidget;
    presenter_->Close();
    presenter_.reset();
    return false;
  }
  bounds_ = gfx::Rect(presenter_->window().x(),presenter_->window().y(),
                      static_cast<int>(presenter_->window().width()),
                      static_cast<int>(presenter_->window().height()));
  if (!properties.bounds.IsEmpty()) SetBoundsInPixels(properties.bounds);
  registry_->AttachPresenter(widget_,presenter_);
  // Delegate may destroy this window. There must be no member access after it.
  delegate_->OnAcceleratedWidgetAvailable(widget_);
  return true;
}
void PlatformWindowCharmOS::Show(bool inactive) {
  if (closed_) return;
  if(state_==PlatformWindowState::kMinimized) visibility_state_=PlatformWindowState::kNormal;
  else visibility_state_.reset();
  presenter_->window().set_visible(true,activatable_ && !inactive);
}
void PlatformWindowCharmOS::Hide() {
  if (!closed_) presenter_->window().set_visible(false,false);
}
void PlatformWindowCharmOS::Close() {
  if (closed_) return;
  closed_ = true;
  UnregisterWidget();
  presenter_->Close();
  delegate_->OnClosed();
}
bool PlatformWindowCharmOS::IsVisible() const { return visible_ && !closed_; }
void PlatformWindowCharmOS::PrepareForShutdown() {
  UnregisterWidget();
  if (presenter_) presenter_->Close();
  closed_ = true;
}
void PlatformWindowCharmOS::SetBoundsInPixels(const gfx::Rect& bounds) {
  QueueBounds(bounds);
}
void PlatformWindowCharmOS::QueueBounds(const gfx::Rect& bounds,
                                        std::optional<PlatformWindowState> state) {
  if(closed_ || bounds.IsEmpty()) return;
  queued_bounds_=bounds; queued_bounds_state_=state;
  SubmitBounds();
}
void PlatformWindowCharmOS::SubmitBounds() {
  if(closed_ || bounds_in_flight_ || !queued_bounds_) return;
  auto status=presenter_->RequestBounds(*queued_bounds_);
  if(status==charm::Status::WouldBlock) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(FROM_HERE,
        base::BindOnce(&PlatformWindowCharmOS::SubmitBounds,weak_factory_.GetWeakPtr()),
        base::Milliseconds(1));
    return;
  }
  if(status!=charm::Status::Ok) {
    LOG(ERROR)<<"Native bounds request failed: "<<static_cast<unsigned>(status);
    queued_bounds_.reset(); queued_bounds_state_.reset(); return;
  }
  bounds_in_flight_=true;
  bounds_state_=queued_bounds_state_;
  queued_bounds_.reset(); queued_bounds_state_.reset();
}
void PlatformWindowCharmOS::UnregisterWidget() {
  if(widget_!=gfx::kNullAcceleratedWidget) {
    registry_->Remove(widget_,this);
    widget_=gfx::kNullAcceleratedWidget;
  }
}
gfx::Rect PlatformWindowCharmOS::GetBoundsInPixels() const { return bounds_; }
void PlatformWindowCharmOS::SetBoundsInDIP(const gfx::Rect& bounds) {
  SetBoundsInPixels(delegate_->ConvertRectToPixels(bounds));
}
gfx::Rect PlatformWindowCharmOS::GetBoundsInDIP() const {
  return delegate_->ConvertRectToDIP(bounds_);
}
void PlatformWindowCharmOS::SetTitle(const std::u16string& title) {
  std::u16string bounded = title;
  std::string utf8 = base::UTF16ToUTF8(bounded);
  while (utf8.size() > 63 && !bounded.empty()) {
    bounded.pop_back();
    utf8 = base::UTF16ToUTF8(bounded);
  }
  if (!closed_) presenter_->window().set_title(utf8.c_str());
}
void PlatformWindowCharmOS::SetCapture() {
  if(!closed_) presenter_->window().set_capture(true);
}
void PlatformWindowCharmOS::ReleaseCapture() {
  if(!closed_) presenter_->window().set_capture(false);
}
bool PlatformWindowCharmOS::HasCapture() const { return !closed_ && presenter_->window().captured(); }
void PlatformWindowCharmOS::SetFullscreen(bool fullscreen,int64_t target_display_id) {
  if (fullscreen && delegate_->CanFullscreen())
    LOG(ERROR) << "Fullscreen is unavailable until WM borderless fullscreen is implemented";
}
void PlatformWindowCharmOS::Maximize() {
  if (closed_ || !delegate_->CanMaximize() || state_ == PlatformWindowState::kMaximized)
    return;
  restored_bounds_ = bounds_;
  auto work = presenter_->window().display().work;
  const auto title=presenter_->window().display().title_height;
  QueueBounds(gfx::Rect(work.x,work.y+title,static_cast<int>(work.width),
                         static_cast<int>(work.height-title)),PlatformWindowState::kMaximized);
}
void PlatformWindowCharmOS::Minimize() {
  if (closed_ || state_ == PlatformWindowState::kMinimized) return;
  visibility_state_ = PlatformWindowState::kMinimized;
  presenter_->window().set_visible(false,false);
}
void PlatformWindowCharmOS::Restore() {
  if (closed_) return;
  visibility_state_ = PlatformWindowState::kNormal;
  if (restored_bounds_) {
    QueueBounds(*restored_bounds_,PlatformWindowState::kNormal);
    restored_bounds_.reset();
  }
  presenter_->window().set_visible(true,true);
}
PlatformWindowState PlatformWindowCharmOS::GetPlatformWindowState() const { return state_; }
void PlatformWindowCharmOS::Activate() {
  if (!closed_ && activatable_) presenter_->window().activate(true);
}
void PlatformWindowCharmOS::Deactivate() {
  if (!closed_) presenter_->window().activate(false);
}
void PlatformWindowCharmOS::SetUseNativeFrame(bool use_native_frame) {
  if (!use_native_frame)
    LOG(WARNING) << "Client-side decorations require WM borderless windows";
}
bool PlatformWindowCharmOS::ShouldUseNativeFrame() const { return true; }
void PlatformWindowCharmOS::SetCursor(scoped_refptr<PlatformCursor> cursor) {
  // The WM owns the pointer bitmap; keeping the platform cursor alive here
  // made Chromium dereference a cursor it never negotiated with the WM (a
  // FlatMap CHECK in the compositor path). Drop it instead.
  (void)cursor;
}
void PlatformWindowCharmOS::MoveCursorTo(const gfx::Point& location) {
  LOG(WARNING) << "Cursor warping is not supported by WM";
}
void PlatformWindowCharmOS::ConfineCursorToBounds(const gfx::Rect& bounds) {
  LOG(WARNING) << "Cursor confinement is not supported by WM";
}
void PlatformWindowCharmOS::SetRestoredBoundsInDIP(const gfx::Rect& bounds) {
  restored_bounds_ = delegate_->ConvertRectToPixels(bounds);
}
gfx::Rect PlatformWindowCharmOS::GetRestoredBoundsInDIP() const {
  return delegate_->ConvertRectToDIP(restored_bounds_.value_or(bounds_));
}
void PlatformWindowCharmOS::SetWindowIcons(const gfx::ImageSkia& window_icon,
                                           const gfx::ImageSkia& app_icon) {
  LOG(WARNING) << "WM window icons are not supported";
}
void PlatformWindowCharmOS::SizeConstraintsChanged() {
  gfx::Rect bounded = bounds_;
  if (auto minimum = delegate_->GetMinimumSizeForWindow()) {
    bounded.set_width(std::max(bounded.width(),minimum->width()));
    bounded.set_height(std::max(bounded.height(),minimum->height()));
  }
  if (auto maximum = delegate_->GetMaximumSizeForWindow()) {
    bounded.set_width(std::min(bounded.width(),maximum->width()));
    bounded.set_height(std::min(bounded.height(),maximum->height()));
  }
  if (bounded != bounds_) SetBoundsInPixels(bounded);
}
void PlatformWindowCharmOS::ApplyBounds() {
  const auto& window = presenter_->window();
  // Drag notifications can precede CONFIGURED for the old generation.
  std::optional<PlatformWindowState> state;
  if(!window.configuration_pending()) {
    bounds_in_flight_=false;
    state=bounds_state_;
    bounds_state_.reset();
  }
  gfx::Rect updated(window.x(),window.y(),static_cast<int>(window.width()),
                    static_cast<int>(window.height()));
  const bool origin = updated.origin() != bounds_.origin();
  bounds_ = updated;
  auto weak = weak_factory_.GetWeakPtr();
  delegate_->OnBoundsChanged({origin});
  if (!weak || closed_) return;
  if(state) UpdateState(*state);
  if(weak) SubmitBounds();
}
void PlatformWindowCharmOS::UpdateState(PlatformWindowState state) {
  if (state_ == state) return;
  const auto old = state_;
  state_ = state;
  delegate_->OnWindowStateChanged(old,state);
}
void PlatformWindowCharmOS::OnConnectionClosed() {
  if (closed_) return;
  closed_ = true;
  visible_ = false;
  UnregisterWidget();
  auto weak = weak_factory_.GetWeakPtr();
  delegate_->OnAcceleratedWidgetDestroyed();
  if (weak) delegate_->OnClosed();
}
void PlatformWindowCharmOS::OnNativeEvent(charm::Status status,
                                          const abi::wm::WindowEvent& event) {
  if (status != charm::Status::Ok) {
    OnConnectionClosed();
    return;
  }
  auto weak = weak_factory_.GetWeakPtr();
  switch (event.type) {
    case abi::wm::kFrameDone:
      return;
    case abi::wm::kConfigured:
      if (!event.value) ApplyBounds();
      else { bounds_in_flight_=false; bounds_state_.reset(); SubmitBounds(); }
      return;
    case abi::wm::kBoundsChanged:
      ApplyBounds();
      return;
    case abi::wm::kVisibilityChanged:
      visible_ = event.value != 0;
      if(visible_) registry_->NoteRaised(widget_);
      delegate_->OnOcclusionStateChanged(visible_ ?
          PlatformWindowOcclusionState::kVisible : PlatformWindowOcclusionState::kHidden);
      if (weak && visibility_state_) {
        const auto pending = *visibility_state_;
        visibility_state_.reset();
        UpdateState(pending);
      }
      return;
    case abi::wm::kFocus:
      active_ = event.value != 0;
      if(active_) registry_->NoteRaised(widget_);
      if(!active_) input_.ResetPressedState();
      delegate_->OnActivationChanged(active_);
      return;
    case abi::wm::kCaptureChanged:
      if(!event.value) delegate_->OnLostCapture();
      return;
    case abi::wm::kCloseRequest:
      delegate_->OnCloseRequest();
      return;
    case abi::wm::kInput: {
      abi::syscall::InputEvent input{};
      while (presenter_->window().next_input(&input)) {
        registry_->NoteInput();
        if (input.type != abi::syscall::kInputKey) {
          registry_->SetCursorScreenPoint(bounds_.origin() +
              gfx::Vector2d(input.x,input.y));
        }
        auto events=input_.Translate(input);
        for(auto& translated:events) {
          delegate_->DispatchEvent(translated.get());
          if(!weak || closed_) return;
        }
      }
      return;
    }
    default:
      return;
  }
}
}  // namespace ui
