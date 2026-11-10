// CharmOS animation accessibility policy.
//
// The window manager does not expose a reduced-motion preference, so rich
// animations are enabled by default until the platform provides one. This is
// the same behavior as the ChromeOS fallback and is queried on the UI thread.
#include "ui/gfx/animation/animation.h"

namespace gfx {

bool Animation::ShouldRenderRichAnimationImpl() {
  if (prefers_reduced_motion_.has_value()) {
    return !prefers_reduced_motion_.value();
  }
  return true;
}

bool Animation::ScrollAnimationsEnabledBySystem() {
  return ShouldRenderRichAnimation();
}

void Animation::UpdatePrefersReducedMotion() {
  if (!prefers_reduced_motion_.has_value()) prefers_reduced_motion_ = false;
}

}  // namespace gfx
