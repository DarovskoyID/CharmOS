// PartitionAlloc stack capture for CharmOS.
//
// The generic Linux unit is not built in this profile, so provide the same
// frame-pointer walker it uses. When frame pointers are disabled the capture
// reports zero frames instead of reading unreliable stack slots.
#include "partition_alloc/build_config.h"
#include "partition_alloc/buildflags.h"
#include "partition_alloc/partition_alloc_base/debug/stack_trace.h"

namespace partition_alloc::internal::base::debug {

size_t CollectStackTrace(const void** trace, size_t count) {
#if PA_BUILDFLAG(CAN_UNWIND_WITH_FRAME_POINTERS)
  return TraceStackFramePointers(trace, count, 0);
#else
  (void)trace;
  (void)count;
  return 0;
#endif
}

}  // namespace partition_alloc::internal::base::debug
