// CharmOS memory metrics.
//
// The upstream Linux implementation depends on fields and test hooks that are
// only compiled for Linux/ChromeOS/Android. CharmOS exposes /proc, so read
// statm and VmHWM directly and report the fields that exist in this build's
// mojom; memory-map enumeration is not available yet.
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <vector>

#include "base/process/process_handle.h"
#include "services/resource_coordinator/public/cpp/memory_instrumentation/os_metrics.h"

namespace memory_instrumentation {

namespace {

pid_t ResolvePid(base::ProcessHandle handle) {
  if (handle == base::kNullProcessHandle) return getpid();
  return static_cast<pid_t>(handle);
}

}  // namespace

// static
bool OSMetrics::FillOSMemoryDump(base::ProcessHandle handle,
                                 const MemDumpFlagSet& /*flags*/,
                                 mojom::RawOSMemDump* dump) {
  if (!dump) return false;
  const pid_t pid = ResolvePid(handle);

  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/statm", pid);
  FILE* statm = fopen(path, "r");
  if (!statm) return false;
  unsigned long size_pages = 0;
  unsigned long resident_pages = 0;
  const int fields = fscanf(statm, "%lu %lu", &size_pages, &resident_pages);
  fclose(statm);
  if (fields != 2) return false;

  const long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) return false;
  dump->resident_set_kb =
      static_cast<uint32_t>(resident_pages * page_size / 1024);

  snprintf(path, sizeof(path), "/proc/%d/status", pid);
  FILE* status = fopen(path, "r");
  if (status) {
    char line[256];
    while (fgets(line, sizeof(line), status)) {
      unsigned long peak_kb = 0;
      if (sscanf(line, "VmHWM: %lu kB", &peak_kb) == 1) {
        dump->peak_resident_set_kb = static_cast<uint32_t>(peak_kb);
        break;
      }
    }
    fclose(status);
  }
  dump->is_peak_rss_resettable = false;
  return true;
}

// static
bool OSMetrics::FillProcessMemoryMaps(base::ProcessHandle /*handle*/,
                                      mojom::MemoryMapOption /*option*/,
                                      mojom::RawOSMemDump* /*dump*/) {
  return false;
}

// static
std::vector<mojom::VmRegionPtr> OSMetrics::GetProcessMemoryMaps(
    base::ProcessHandle /*handle*/) {
  return {};
}

}  // namespace memory_instrumentation
