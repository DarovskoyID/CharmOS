#include "base/threading/platform_thread.h"
#include "base/check.h"
#include "base/logging.h"
#include <charm/sys.hpp>

namespace base {
namespace {
uint32_t NativePriority(ThreadType type) {
  switch(type) {
    case ThreadType::kBackground: return 4;
    case ThreadType::kUtility: return 8;
    case ThreadType::kDefault: return 16;
    case ThreadType::kPresentation: return 20;
    case ThreadType::kAudioProcessing: return 24;
    case ThreadType::kRealtimeAudio: return 28;
  }
  CHECK(false);
}
}
// libc owns thread setup, the emulated TLS block, destructors and detached
// reclamation. Chromium has no additional per-thread runtime to initialize.
void InitThreading() {}
void TerminateOnThread() {}
size_t GetDefaultThreadStackSize(const pthread_attr_t&) { return 2*1024*1024; }

void PlatformThreadBase::SetName(const std::string& name) {
  // This is Chromium's diagnostic name registry. Native kernel thread names
  // cannot currently be changed after creation.
  SetNameCommon(name);
}
bool PlatformThreadBase::CanChangeThreadType(ThreadType from,ThreadType to) {
  auto ceiling=charm::runtime_query(8);
  return from==to || (ceiling.ok() && NativePriority(to)<=ceiling.value);
}
ThreadType PlatformThreadBase::GetCurrentEffectiveThreadTypeForTest() {
  auto priority=charm::thread_priority();
  CHECK(priority.ok());
  for(auto type:{ThreadType::kRealtimeAudio,ThreadType::kAudioProcessing,ThreadType::kPresentation,
                 ThreadType::kDefault,ThreadType::kUtility,ThreadType::kBackground})
    if(priority.value>=NativePriority(type)) return type;
  return ThreadType::kBackground;
}
namespace internal {
void SetCurrentThreadTypeImpl(ThreadType type,MessagePumpType) {
  auto result=charm::thread_priority(NativePriority(type));
  CHECK(result.ok());
}
PlatformPriorityOverride SetThreadTypeOverride(PlatformThreadHandle,ThreadType) {
  return false;
}
void RemoveThreadTypeOverride(PlatformThreadHandle,const PlatformPriorityOverride& applied,ThreadType) {
  CHECK(!applied);
}
}
}  // namespace base
