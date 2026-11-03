#include "base/process/process.h"
#include "base/process/kill.h"
#include "base/check.h"
#include "base/files/file_path.h"
#include "base/threading/scoped_blocking_call.h"
#include <charm/process.hpp>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <utility>

namespace base {
namespace {
bool KernelHandle(ProcessHandle handle) { return handle>0 && handle<=UINT32_MAX; }
int NativeError(charm::Status status) {
  switch(status) {
    case charm::Status::TimedOut: return ETIMEDOUT;
    case charm::Status::BadHandle: return EBADF;
    case charm::Status::AccessDenied: return EACCES;
    case charm::Status::Busy: return EBUSY;
    case charm::Status::NoMemory: return ENOMEM;
    case charm::Status::WrongType:
    case charm::Status::InvalidArgs: return EINVAL;
    default: return EIO;
  }
}
uint64_t Deadline(TimeDelta timeout) {
  if(timeout.is_max()) return charm::kForever;
  uint64_t now=charm::now_ns();
  if(timeout.is_negative() || timeout.is_zero()) return now;
  uint64_t delay=static_cast<uint64_t>(timeout.InNanoseconds());
  return delay>=UINT64_MAX-now ? UINT64_MAX-1 : now+delay;
}
}
ProcessId GetCurrentProcId() { return getpid(); }
ProcessHandle GetCurrentProcessHandle() { return kCurrentProcessHandle; }
ProcessId GetProcId(ProcessHandle handle) {
  if(handle==kCurrentProcessHandle) return GetCurrentProcId();
  if(!KernelHandle(handle)) { errno=EBADF; return kNullProcessId; }
  abi::process::ProcessInfo information{};
  if(charm::process_info(static_cast<charm::Handle>(handle),&information)!=charm::Status::Ok) {
    errno=EBADF; return kNullProcessId;
  }
  if(information.pid>INT32_MAX) { errno=EOVERFLOW; return kNullProcessId; }
  return static_cast<ProcessId>(information.pid);
}
ProcessId GetParentProcessId(ProcessHandle) { errno=ENOTSUP; return -1; }
FilePath GetProcessExecutablePath(ProcessHandle) { errno=ENOTSUP; return {}; }

Process::Process(ProcessHandle handle):process_(handle) {}
Process::Process(Process&& other):process_(other.Release()) {}
Process& Process::operator=(Process&& other) {
  if(this!=&other) { Close(); process_=other.Release(); }
  return *this;
}
Process::~Process() { Close(); }
Process Process::Current() { return Process(kCurrentProcessHandle); }
Process Process::Open(ProcessId pid) {
  if(pid==GetCurrentProcId()) return Current();
  errno=EPERM;
  return Process();
}
Process Process::OpenWithExtraPrivileges(ProcessId pid) { return Open(pid); }
bool Process::CanSetPriority() { return false; }
void Process::TerminateCurrentProcessImmediately(int exit_code) { charm::process_exit(exit_code); }
bool Process::IsValid() const { return is_current() || KernelHandle(process_); }
ProcessHandle Process::Handle() const { return process_; }
Process Process::Duplicate() const {
  if(is_current()) return Current();
  if(!KernelHandle(process_)) return Process();
  auto information=charm::handle_rights(static_cast<charm::Handle>(process_));
  if(!information.ok()) return Process();
  auto copy=charm::handle_duplicate(static_cast<charm::Handle>(process_),static_cast<uint32_t>(information.value));
  return copy.ok() ? Process(static_cast<ProcessHandle>(copy.value)) : Process();
}
ProcessHandle Process::Release() { return std::exchange(process_,kNullProcessHandle); }
ProcessId Process::Pid() const { return GetProcId(process_); }
bool Process::is_current() const { return process_==kCurrentProcessHandle; }
void Process::Close() {
  if(KernelHandle(process_)) (void)charm::handle_close(static_cast<charm::Handle>(process_));
  process_=kNullProcessHandle;
}
Time Process::CreationTime() const { return Time(); }  // Native metadata does not expose a timestamp.
bool Process::Terminate(int exit_code,bool wait) const { return TerminateInternal(exit_code,wait); }
bool Process::TerminateInternal(int,bool wait) const {
  if(!KernelHandle(process_)) { errno=is_current() ? EDEADLK : EBADF; return false; }
  auto status=charm::ProcessView(static_cast<charm::Handle>(process_)).terminate();
  if(status!=charm::Status::Ok) { errno=NativeError(status); return false; }
  return !wait || WaitForExitWithTimeout(Seconds(60),nullptr);
}
bool Process::WaitForExit(int* exit_code) const { return WaitForExitWithTimeout(TimeDelta::Max(),exit_code); }
bool Process::WaitForExitWithTimeout(TimeDelta timeout,int* exit_code) const {
  return WaitForExitWithTimeoutImpl(process_,exit_code,timeout);
}
bool Process::WaitForExitWithTimeoutImpl(ProcessHandle handle,int* exit_code,TimeDelta timeout) const {
  if(!KernelHandle(handle)) { errno=handle==kCurrentProcessHandle ? EDEADLK : EBADF; return false; }
  ScopedBlockingCall blocking(FROM_HERE,BlockingType::MAY_BLOCK);
  charm::ProcessView process(static_cast<charm::Handle>(handle));
  uint64_t deadline=Deadline(timeout);
  int64_t result=0;
  charm::Status status;
  do { status=process.wait(deadline,&result); } while(status==charm::Status::Interrupted);
  if(status!=charm::Status::Ok) { errno=NativeError(status); return false; }
  if(result<INT_MIN || result>INT_MAX) { errno=EOVERFLOW; return false; }
  if(exit_code) *exit_code=static_cast<int>(result);
  return true;
}
Process::Priority Process::GetPriority() const { return Priority::kUserBlocking; }
bool Process::SetPriority(Priority) { errno=ENOTSUP; return false; }
int Process::GetOSPriority() const { errno=ENOTSUP; return -1; }
}  // namespace base
