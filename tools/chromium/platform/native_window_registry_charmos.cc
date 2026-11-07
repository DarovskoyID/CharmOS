#include "build/charmos/native_window_registry_charmos.h"
#include <algorithm>

#include "base/check.h"
#include "base/check_op.h"
#include "build/charmos/platform_window_charmos.h"

namespace ui {
NativeWindowRegistryCharmOS::NativeWindowRegistryCharmOS() = default;
NativeWindowRegistryCharmOS::~NativeWindowRegistryCharmOS() {
  DCHECK(thread_checker_.CalledOnValidThread());
}
gfx::AcceleratedWidget NativeWindowRegistryCharmOS::Add(PlatformWindowCharmOS* window) {
  DCHECK(thread_checker_.CalledOnValidThread());
  gfx::AcceleratedWidget widget=windows_.Add(window);
  stacking_.push_back(widget);
  return widget;
}
void NativeWindowRegistryCharmOS::Remove(gfx::AcceleratedWidget widget,
                                         PlatformWindowCharmOS* window) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK_EQ(windows_.Lookup(widget), window);
  scoped_refptr<NativeWindowPresenterCharmOS> retired;
  {
    base::AutoLock lock(presenters_lock_);
    auto found=presenters_.find(widget);
    if(found!=presenters_.end()) {
      retired=std::move(found->second);
      presenters_.erase(found);
    }
  }
  windows_.Remove(widget);
  std::erase(stacking_,widget);
}
void NativeWindowRegistryCharmOS::AttachPresenter(
    gfx::AcceleratedWidget widget,scoped_refptr<NativeWindowPresenterCharmOS> presenter) {
  DCHECK(thread_checker_.CalledOnValidThread());
  CHECK(windows_.Lookup(widget));
  base::AutoLock lock(presenters_lock_);
  presenters_.insert_or_assign(widget,std::move(presenter));
}
scoped_refptr<NativeWindowPresenterCharmOS> NativeWindowRegistryCharmOS::AcquirePresenter(
    gfx::AcceleratedWidget widget) const {
  base::AutoLock lock(presenters_lock_);
  auto found=presenters_.find(widget);
  return found==presenters_.end() ? nullptr : found->second;
}
PlatformWindowCharmOS* NativeWindowRegistryCharmOS::Get(gfx::AcceleratedWidget widget) const {
  DCHECK(thread_checker_.CalledOnValidThread());
  return windows_.Lookup(widget);
}
gfx::AcceleratedWidget NativeWindowRegistryCharmOS::AtPoint(const gfx::Point& point) const {
  DCHECK(thread_checker_.CalledOnValidThread());
  for(auto it=stacking_.rbegin();it!=stacking_.rend();++it) {
    const auto* window = windows_.Lookup(*it);
    if (window->IsVisible() && window->GetBoundsInPixels().Contains(point))
      return *it;
  }
  return gfx::kNullAcceleratedWidget;
}
void NativeWindowRegistryCharmOS::NoteRaised(gfx::AcceleratedWidget widget) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if(!windows_.Lookup(widget)) return;
  std::erase(stacking_,widget);
  stacking_.push_back(widget);
}
void NativeWindowRegistryCharmOS::SetCursorScreenPoint(const gfx::Point& point) {
  DCHECK(thread_checker_.CalledOnValidThread());
  cursor_screen_point_ = point;
}
void NativeWindowRegistryCharmOS::NoteInput() {
  DCHECK(thread_checker_.CalledOnValidThread());
  last_input_ = base::TimeTicks::Now();
}
}  // namespace ui
