// CharmOS idle and screen-lock state.
//
// CharmOS input events are delivered through the window manager door. The
// Ozone event source calls NotifyUserActivity() when a key or pointer event
// arrives, so idle time reflects real user input rather than polling a desktop
// session bus. Screen locking is not implemented yet and reports unlocked.
#include <atomic>
#include <utility>

#include "base/atomicops.h"
#include "base/callback_list.h"
#include "base/no_destructor.h"
#include "base/time/time.h"
#include "ui/base/idle/idle.h"

namespace ui {

namespace {

std::atomic<int64_t> g_last_activity_us{0};
std::atomic<bool> g_locked{false};
base::RepeatingCallbackList<void(bool)>* Callbacks() {
  static base::NoDestructor<base::RepeatingCallbackList<void(bool)>> list;
  return list.get();
}

int64_t NowUs() {
  return base::TimeTicks::Now().since_origin().InMicroseconds();
}

void TouchIfUnset() {
  int64_t expected = 0;
  g_last_activity_us.compare_exchange_strong(expected, NowUs());
}

}  // namespace

void NotifyUserActivity() {
  g_last_activity_us.store(NowUs(), std::memory_order_relaxed);
}

int CalculateIdleTime() {
  TouchIfUnset();
  const int64_t idle_us = NowUs() - g_last_activity_us.load();
  return static_cast<int>(idle_us / base::Time::kMicrosecondsPerSecond);
}

IdleState CalculateIdleState(int idle_threshold) {
  if (CheckIdleStateIsLocked()) return IDLE_STATE_LOCKED;
  return CalculateIdleTime() >= idle_threshold ? IDLE_STATE_IDLE
                                               : IDLE_STATE_ACTIVE;
}

bool CheckIdleStateIsLocked() {
  return g_locked.load(std::memory_order_relaxed);
}

base::CallbackListSubscription AddScreenLockCallback(
    base::RepeatingCallback<void(bool)> callback) {
  return Callbacks()->Add(std::move(callback));
}

}  // namespace ui
