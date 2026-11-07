#include "base/memory/platform_shared_memory_region.h"
#include "base/check.h"
#include "base/types/expected.h"
#include <charm/memory.hpp>
#include <utility>

namespace base::subtle {
namespace {
constexpr uint32_t common_rights=charm::rights::kRead|charm::rights::kMap|
                                  charm::rights::kDuplicate|charm::rights::kTransfer;
uint32_t RightsFor(PlatformSharedMemoryRegion::Mode mode) {
  return common_rights|(mode==PlatformSharedMemoryRegion::Mode::kReadOnly ? 0u : charm::rights::kWrite);
}
}

expected<PlatformSharedMemoryRegion,PlatformSharedMemoryRegion::TakeError>
PlatformSharedMemoryRegion::TakeOrFail(ScopedPlatformSharedMemoryHandle handle,
                                      Mode mode,size_t size,const UnguessableToken& guid) {
  if(!handle) return unexpected(TakeError::kInvalidNativeHandle);
  if(!size || size>abi::syscall::kVmoMaxSize) return unexpected(TakeError::kNativeSizeMismatch);
  auto permissions=CheckPlatformHandlePermissionsCorrespondToMode(handle.get(),mode,size);
  if(!permissions.has_value()) return unexpected(permissions.error());
  return PlatformSharedMemoryRegion(std::move(handle),mode,size,guid);
}
PlatformSharedMemoryHandle PlatformSharedMemoryRegion::GetPlatformHandle() const { return handle_.get(); }
bool PlatformSharedMemoryRegion::IsValid() const { return static_cast<bool>(handle_); }

PlatformSharedMemoryRegion PlatformSharedMemoryRegion::Duplicate() const {
  if(!IsValid()) return {};
  CHECK(mode_!=Mode::kWritable);
  charm::UniqueHandle copy;
  if(handle_.duplicate(RightsFor(mode_),&copy)!=charm::Status::Ok) return {};
  return PlatformSharedMemoryRegion(std::move(copy),mode_,size_,guid_);
}
bool PlatformSharedMemoryRegion::ConvertToReadOnly() {
  if(!IsValid()) return false;
  CHECK(mode_==Mode::kWritable);
  charm::UniqueHandle reduced;
  if(handle_.duplicate(common_rights,&reduced)!=charm::Status::Ok) { handle_.reset(); return false; }
  handle_=std::move(reduced);
  mode_=Mode::kReadOnly;
  return true;
}
bool PlatformSharedMemoryRegion::ConvertToUnsafe() {
  if(!IsValid()) return false;
  CHECK(mode_==Mode::kWritable);
  mode_=Mode::kUnsafe;
  return true;
}
PlatformSharedMemoryRegion PlatformSharedMemoryRegion::Create(Mode mode,size_t size) {
  if(!size || size>abi::syscall::kVmoMaxSize) return {};
  CHECK(mode!=Mode::kReadOnly);
  charm::SharedMemory allocation;
  if(charm::SharedMemory::create(size,&allocation)!=charm::Status::Ok) return {};
  charm::UniqueHandle owner;
  if(allocation.share(RightsFor(mode),&owner)!=charm::Status::Ok) return {};
  return PlatformSharedMemoryRegion(std::move(owner),mode,size,UnguessableToken::Create());
}
expected<void,PlatformSharedMemoryRegion::TakeError>
PlatformSharedMemoryRegion::CheckPlatformHandlePermissionsCorrespondToMode(
    PlatformSharedMemoryHandle handle,Mode mode,size_t size) {
  if(mode!=Mode::kReadOnly && mode!=Mode::kWritable && mode!=Mode::kUnsafe)
    return unexpected(TakeError::kInvalidNativeHandle);
  auto information=charm::vmo_size(handle);
  if(!information.ok()) return unexpected(TakeError::kInvalidNativeHandle);
  if(!size || size>abi::syscall::kVmoMaxSize || information.value!=((size+4095)&~size_t{4095}))
    return unexpected(TakeError::kNativeSizeMismatch);
  auto permissions=charm::handle_rights(handle);
  if(!permissions.ok() || (permissions.value&common_rights)!=common_rights)
    return unexpected(TakeError::kInvalidNativeHandle);
  bool can_write=(permissions.value&charm::rights::kWrite)!=0;
  if((mode==Mode::kReadOnly)==can_write)
    return unexpected(mode==Mode::kReadOnly ? TakeError::kExpectedReadOnlyButNot : TakeError::kExpectedWritableButNot);
  return ok();
}
PlatformSharedMemoryRegion::PlatformSharedMemoryRegion(
    ScopedPlatformSharedMemoryHandle handle,Mode mode,size_t size,const UnguessableToken& guid)
    :handle_(std::move(handle)),mode_(mode),size_(size),guid_(guid) {}
}  // namespace base::subtle
