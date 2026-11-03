#include "base/process/process_metrics.h"
#include <charm/sys.hpp>

namespace base {
bool GetSystemMemoryInfo(SystemMemoryInfo* information) {
  if (!information) return false;
  auto total=charm::runtime_query(5);
  auto available=charm::runtime_query(6);
  if (!total.ok() || !available.ok() || available.value>total.value) return false;
  SystemMemoryInfo result;
  result.total=ByteSize(total.value*4096);
  result.free=ByteSize(available.value*4096);
  // CharmOS has no swap subsystem.
  result.swap_total=ByteSize(0);
  result.swap_free=ByteSize(0);
  *information=result;
  return true;
}
ByteSize SystemMemoryInfo::GetAvailablePhysicalMemory() const { return free; }
}  // namespace base
