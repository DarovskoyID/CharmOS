#ifndef CHARMOS_NATIVE_WINDOW_REGISTRY_H_
#define CHARMOS_NATIVE_WINDOW_REGISTRY_H_

#include "base/containers/id_map.h"
#include "base/containers/flat_map.h"
#include "base/memory/scoped_refptr.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread_checker.h"
#include "base/time/time.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/native_ui_types.h"
#include <vector>

namespace ui {
class PlatformWindowCharmOS;
class NativeWindowPresenterCharmOS;

class NativeWindowRegistryCharmOS {
 public:
  NativeWindowRegistryCharmOS();
  ~NativeWindowRegistryCharmOS();
  NativeWindowRegistryCharmOS(const NativeWindowRegistryCharmOS&) = delete;
  NativeWindowRegistryCharmOS& operator=(const NativeWindowRegistryCharmOS&) = delete;

  gfx::AcceleratedWidget Add(PlatformWindowCharmOS* window);
  void Remove(gfx::AcceleratedWidget widget, PlatformWindowCharmOS* window);
  PlatformWindowCharmOS* Get(gfx::AcceleratedWidget widget) const;
  void AttachPresenter(gfx::AcceleratedWidget widget,
                       scoped_refptr<NativeWindowPresenterCharmOS> presenter);
  scoped_refptr<NativeWindowPresenterCharmOS> AcquirePresenter(gfx::AcceleratedWidget widget) const;
  gfx::AcceleratedWidget AtPoint(const gfx::Point& point) const;
  void NoteRaised(gfx::AcceleratedWidget widget);
  void SetCursorScreenPoint(const gfx::Point& point);
  const gfx::Point& cursor_screen_point() const { return cursor_screen_point_; }
  void NoteInput();
  base::TimeDelta IdleTime() const { return base::TimeTicks::Now() - last_input_; }

 private:
  mutable base::IDMap<PlatformWindowCharmOS*> windows_;
  std::vector<gfx::AcceleratedWidget> stacking_;
  mutable base::Lock presenters_lock_;
  base::flat_map<gfx::AcceleratedWidget,scoped_refptr<NativeWindowPresenterCharmOS>> presenters_;
  gfx::Point cursor_screen_point_;
  base::TimeTicks last_input_ = base::TimeTicks::Now();
  base::ThreadChecker thread_checker_;
};
}  // namespace ui
#endif
