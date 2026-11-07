// CharmOS glue for V8's portable platform layer.
//
// V8 implements these OS hooks in platform-linux.cc, which is not part of the
// CharmOS source selection. They are small and do not need Linux: there are no
// shared libraries to enumerate, no code-moving-GC signal, and no scheduler
// tuning. CharmOS keeps a fixed UTC timezone, so the timezone cache is a
// trivial UTC implementation.
#include <math.h>

#include <ostream>
#include <vector>

#include "src/base/debug/stack_trace.h"
#include "src/base/platform/platform.h"
#include "src/base/timezone-cache.h"

namespace v8 {
namespace base {

namespace {

class CharmosTimezoneCache final : public TimezoneCache {
 public:
  const char* LocalTimezone(double) override { return "UTC"; }
  double DaylightSavingsOffset(double) override { return 0; }
  double LocalTimeOffset(double, bool) override { return 0; }
  void Clear(TimeZoneDetection) override {}
};

}  // namespace

std::vector<OS::SharedLibraryAddress> OS::GetSharedLibraryAddresses() {
  return {};
}

void OS::SignalCodeMovingGC() {}

void OS::AdjustSchedulingParams() {}

TimezoneCache* OS::CreateTimezoneCache() {
  return new CharmosTimezoneCache();
}

}  // namespace base

namespace base {
namespace debug {

// CharmOS has no execinfo/backtrace backend. Stack traces are unavailable, but
// the default platform's failure path still calls these entry points.
bool EnableInProcessStackDumping() { return false; }

void DisableSignalStackDump() {}

StackTrace::StackTrace() = default;

void StackTrace::Print() const {}

void StackTrace::OutputToStream(std::ostream*) const {}

}  // namespace debug
}  // namespace base
}  // namespace v8

// The LLVM numeric runtime used by CharmOS implements double pow() but not the
// long-double variant. V8's Math/date code only needs double precision.
extern "C" long double powl(long double x, long double y) {
  return static_cast<long double>(
      ::pow(static_cast<double>(x), static_cast<double>(y)));
}
