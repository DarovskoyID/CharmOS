#include "base/process/process_metrics.h"
#include "base/memory/ptr_util.h"
#include <charm/libc.h>
#include <charm/sys.hpp>
#include <errno.h>
#include <limits.h>
#include <sys/resource.h>
#include <unistd.h>

namespace base {
ProcessMetrics::ProcessMetrics(ProcessHandle process):process_(process) {}

std::unique_ptr<ProcessMetrics> ProcessMetrics::CreateProcessMetrics(ProcessHandle process) {
  // The native ABI exposes CPU/allocator/descriptor accounting only for self.
  if(process!=GetCurrentProcessHandle()) { errno=ENOTSUP; return nullptr; }
  return WrapUnique(new ProcessMetrics(process));
}

expected<TimeDelta,ProcessCPUUsageError> ProcessMetrics::GetCumulativeCPUUsage() {
  auto charged=charm::runtime_query(2);
  if(!charged.ok() || charged.value>INT64_MAX) {
    errno=charged.ok() ? EOVERFLOW : EIO;
    return unexpected(ProcessCPUUsageError::kSystemError);
  }
  return Nanoseconds(static_cast<int64_t>(charged.value));
}

expected<ProcessMemoryInfo,ProcessUsageError> ProcessMetrics::GetMemoryInfo() const {
  errno=ENOTSUP; // Allocator bytes are not process-wide resident pages.
  return unexpected(ProcessUsageError::kSystemError);
}

int ProcessMetrics::GetIdleWakeupsPerSecond() { errno=ENOTSUP; return -1; }
int ProcessMetrics::GetOpenFdCount() const { return charm_open_fd_count(); }
int ProcessMetrics::GetOpenFdSoftLimit() const {
  rlimit limits{};
  if(getrlimit(RLIMIT_NOFILE,&limits)) return -1;
  if(limits.rlim_cur>INT_MAX) { errno=EOVERFLOW; return -1; }
  return static_cast<int>(limits.rlim_cur);
}
size_t ProcessMetrics::GetMallocUsage() { return charm_heap_allocated_bytes(); }

size_t GetMaxFds() {
  long limit=sysconf(_SC_OPEN_MAX);
  return limit<0 ? 0 : static_cast<size_t>(limit);
}
size_t GetHandleLimit() {
  // Absolute ABI table ceiling; individual Jobs may constrain it further.
  return abi::syscall::kMaxHandles;
}
void IncreaseFdLimitTo(unsigned int requested) {
  rlimit limits{};
  if(getrlimit(RLIMIT_NOFILE,&limits) || requested<=limits.rlim_cur) return;
  limits.rlim_cur=requested<limits.rlim_max ? requested : limits.rlim_max;
  (void)setrlimit(RLIMIT_NOFILE,&limits);
}
size_t GetSystemCommitCharge() {
  errno=ENOTSUP; // There is no native overcommit/commit accounting contract.
  return 0;
}
int64_t TimeValToMicroseconds(const timeval& value) {
  return value.tv_sec*Time::kMicrosecondsPerSecond+value.tv_usec;
}
} // namespace base
