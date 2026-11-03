#include "base/process/kill.h"
#include "base/check.h"
#include <charm/process.hpp>
#include <errno.h>
#include <limits.h>
#include <thread>
#include <utility>

namespace base {
TerminationStatus GetTerminationStatus(ProcessHandle handle,int* exit_code) {
  CHECK(exit_code);
  *exit_code=-1;
  if(handle==kCurrentProcessHandle) return TERMINATION_STATUS_STILL_RUNNING;
  if(handle<=0 || handle>UINT32_MAX) { errno=EBADF; return TERMINATION_STATUS_LAUNCH_FAILED; }
  abi::process::ProcessInfo information{};
  if(charm::process_info(static_cast<charm::Handle>(handle),&information)!=charm::Status::Ok) {
    errno=EBADF; return TERMINATION_STATUS_LAUNCH_FAILED;
  }
  if(information.state!=2) return TERMINATION_STATUS_STILL_RUNNING;
  if(information.exit_code>=INT_MIN && information.exit_code<=INT_MAX)
    *exit_code=static_cast<int>(information.exit_code);
  else errno=EOVERFLOW;
  // The kernel currently uses -1000 for both faults and forced termination;
  // do not invent a distinction between killed/crashed/OOM in diagnostics.
  return information.exit_code==0 ? TERMINATION_STATUS_NORMAL_TERMINATION : TERMINATION_STATUS_ABNORMAL_TERMINATION;
}
TerminationStatus GetKnownDeadTerminationStatus(ProcessHandle handle,int* exit_code) {
  auto status=GetTerminationStatus(handle,exit_code);
  if(status!=TERMINATION_STATUS_STILL_RUNNING || handle==kCurrentProcessHandle) return status;
  charm::ProcessView process(static_cast<charm::Handle>(handle));
  if(process.terminate()!=charm::Status::Ok) return status;
  uint64_t deadline=charm::now_ns()+60'000'000'000;
  charm::Status waited;
  do { waited=process.wait(deadline); } while(waited==charm::Status::Interrupted);
  return GetTerminationStatus(handle,exit_code);
}
bool KillProcesses(const FilePath::StringType&,int,const ProcessFilter*) {
  errno=ENOTSUP; return false;
}
bool WaitForProcessesToExit(const FilePath::StringType&,TimeDelta,const ProcessFilter*) {
  errno=ENOTSUP; return false;
}
bool CleanupProcesses(const FilePath::StringType&,TimeDelta,int,const ProcessFilter*) {
  errno=ENOTSUP; return false;
}
void EnsureProcessTerminated(Process process) {
  CHECK(!process.is_current());
  if(!process.IsValid() || process.WaitForExitWithTimeout(TimeDelta(),nullptr)) return;
  // Native handles retain exit metadata; no POSIX child-reaping thread exists.
  std::thread([owned=std::move(process)]() mutable {
    if(!owned.WaitForExitWithTimeout(Seconds(2),nullptr)) (void)owned.Terminate(-1,true);
  }).detach();
}
}  // namespace base
