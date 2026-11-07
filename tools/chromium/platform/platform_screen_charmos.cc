#include "build/charmos/platform_screen_charmos.h"

#include "base/check.h"
#include "build/charmos/native_window_registry_charmos.h"

namespace ui {
PlatformScreenCharmOS::PlatformScreenCharmOS(
    NativeWindowRegistryCharmOS* registry,const gfx::Rect& bounds,
    const gfx::Rect& work_area) : registry_(registry) {
  display::Display display(1,bounds);
  display.set_work_area(work_area);
  displays_.AddDisplay(display,display::DisplayList::Type::PRIMARY);
}
PlatformScreenCharmOS::~PlatformScreenCharmOS() = default;
const std::vector<display::Display>& PlatformScreenCharmOS::GetAllDisplays() const {
  return displays_.displays();
}
display::Display PlatformScreenCharmOS::GetPrimaryDisplay() const {
  auto it = displays_.GetPrimaryDisplayIterator();
  CHECK(it != displays_.displays().end());
  return *it;
}
display::Display PlatformScreenCharmOS::GetDisplayForAcceleratedWidget(
    gfx::AcceleratedWidget widget) const {
  return GetPrimaryDisplay();
}
gfx::Point PlatformScreenCharmOS::GetCursorScreenPoint() const {
  return registry_->cursor_screen_point();
}
gfx::AcceleratedWidget PlatformScreenCharmOS::GetAcceleratedWidgetAtScreenPoint(
    const gfx::Point& point) const {
  return registry_->AtPoint(point);
}
display::Display PlatformScreenCharmOS::GetDisplayNearestPoint(
    const gfx::Point& point) const {
  return GetPrimaryDisplay();
}
display::Display PlatformScreenCharmOS::GetDisplayMatching(
    const gfx::Rect& match_rect) const {
  return GetPrimaryDisplay();
}
bool PlatformScreenCharmOS::IsScreenSaverActive() const { return false; }
base::TimeDelta PlatformScreenCharmOS::CalculateIdleTime() const {
  return registry_->IdleTime();
}
void PlatformScreenCharmOS::AddObserver(display::DisplayObserver* observer) {
  displays_.AddObserver(observer);
}
void PlatformScreenCharmOS::RemoveObserver(display::DisplayObserver* observer) {
  displays_.RemoveObserver(observer);
}
bool PlatformScreenCharmOS::IsHeadless() const { return false; }
}  // namespace ui
