#ifndef CHARMOS_PLATFORM_WINDOW_H_
#define CHARMOS_PLATFORM_WINDOW_H_

#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "build/charmos/input_translator_charmos.h"
#include "build/charmos/native_window_presenter_charmos.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/platform_window/platform_window.h"
#include "ui/platform_window/platform_window_init_properties.h"

namespace ui {
class NativeWindowRegistryCharmOS;

class PlatformWindowCharmOS final : public PlatformWindow {
 public:
  PlatformWindowCharmOS(PlatformWindowDelegate* delegate,
                        NativeWindowRegistryCharmOS* registry);
  ~PlatformWindowCharmOS() override;
  PlatformWindowCharmOS(const PlatformWindowCharmOS&) = delete;
  PlatformWindowCharmOS& operator=(const PlatformWindowCharmOS&) = delete;

  bool Initialize(const PlatformWindowInitProperties& properties);
  const scoped_refptr<NativeWindowPresenterCharmOS>& presenter() const { return presenter_; }
  gfx::AcceleratedWidget widget() const { return widget_; }

  void Show(bool inactive=false) override;
  void Hide() override;
  void Close() override;
  bool IsVisible() const override;
  void PrepareForShutdown() override;
  void SetBoundsInPixels(const gfx::Rect& bounds) override;
  gfx::Rect GetBoundsInPixels() const override;
  void SetBoundsInDIP(const gfx::Rect& bounds) override;
  gfx::Rect GetBoundsInDIP() const override;
  void SetTitle(const std::u16string& title) override;
  void SetCapture() override;
  void ReleaseCapture() override;
  bool HasCapture() const override;
  void SetFullscreen(bool fullscreen,int64_t target_display_id) override;
  void Maximize() override;
  void Minimize() override;
  void Restore() override;
  PlatformWindowState GetPlatformWindowState() const override;
  void Activate() override;
  void Deactivate() override;
  void SetUseNativeFrame(bool use_native_frame) override;
  bool ShouldUseNativeFrame() const override;
  void SetCursor(scoped_refptr<PlatformCursor> cursor) override;
  void MoveCursorTo(const gfx::Point& location) override;
  void ConfineCursorToBounds(const gfx::Rect& bounds) override;
  void SetRestoredBoundsInDIP(const gfx::Rect& bounds) override;
  gfx::Rect GetRestoredBoundsInDIP() const override;
  void SetWindowIcons(const gfx::ImageSkia& window_icon,
                      const gfx::ImageSkia& app_icon) override;
  void SizeConstraintsChanged() override;

 private:
  void OnNativeEvent(charm::Status status,const abi::wm::WindowEvent& event);
  void ApplyBounds();
  void UpdateState(PlatformWindowState state);
  void OnConnectionClosed();
  void UnregisterWidget();
  void QueueBounds(const gfx::Rect& bounds,std::optional<PlatformWindowState> state=std::nullopt);
  void SubmitBounds();
  raw_ptr<PlatformWindowDelegate> delegate_;
  raw_ptr<NativeWindowRegistryCharmOS> registry_;
  scoped_refptr<NativeWindowPresenterCharmOS> presenter_;
  gfx::AcceleratedWidget widget_ = gfx::kNullAcceleratedWidget;
  gfx::Rect bounds_;
  std::optional<gfx::Rect> restored_bounds_;
  std::optional<PlatformWindowState> bounds_state_,visibility_state_,queued_bounds_state_;
  std::optional<gfx::Rect> queued_bounds_;
  bool bounds_in_flight_=false;
  bool activatable_=true;
  PlatformWindowState state_ = PlatformWindowState::kNormal;
  InputTranslatorCharmOS input_;
  scoped_refptr<PlatformCursor> cursor_;
  bool visible_ = false;
  bool active_ = false;
  bool closed_ = false;
  base::WeakPtrFactory<PlatformWindowCharmOS> weak_factory_{this};
};
}  // namespace ui
#endif
