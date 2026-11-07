#ifndef CHARMOS_PLATFORM_SCREEN_H_
#define CHARMOS_PLATFORM_SCREEN_H_

#include "base/memory/raw_ptr.h"
#include "ui/display/display_list.h"
#include "ui/ozone/public/platform_screen.h"

namespace ui {
class NativeWindowRegistryCharmOS;

class PlatformScreenCharmOS final : public PlatformScreen {
 public:
  PlatformScreenCharmOS(NativeWindowRegistryCharmOS* registry,
                        const gfx::Rect& bounds,const gfx::Rect& work_area);
  ~PlatformScreenCharmOS() override;
  const std::vector<display::Display>& GetAllDisplays() const override;
  display::Display GetPrimaryDisplay() const override;
  display::Display GetDisplayForAcceleratedWidget(
      gfx::AcceleratedWidget widget) const override;
  gfx::Point GetCursorScreenPoint() const override;
  gfx::AcceleratedWidget GetAcceleratedWidgetAtScreenPoint(
      const gfx::Point& point) const override;
  display::Display GetDisplayNearestPoint(const gfx::Point& point) const override;
  display::Display GetDisplayMatching(const gfx::Rect& match_rect) const override;
  bool IsScreenSaverActive() const override;
  base::TimeDelta CalculateIdleTime() const override;
  void AddObserver(display::DisplayObserver* observer) override;
  void RemoveObserver(display::DisplayObserver* observer) override;
  bool IsHeadless() const override;

 private:
  raw_ptr<NativeWindowRegistryCharmOS> registry_;
  display::DisplayList displays_;
};
}  // namespace ui
#endif
