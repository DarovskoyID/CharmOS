#include "base/system/sys_info.h"
#include "base/check.h"
#include "base/cpu.h"
#include "base/files/drive_info.h"
#include <charm/sys.hpp>
#include <sys/utsname.h>
#include <unistd.h>
#include <ostream>

namespace base {
namespace {
utsname NativeIdentity() {
  utsname identity{};
  CHECK(uname(&identity)==0);
  return identity;
}
ByteSize FrameCount(uint64_t selector) {
  auto result=charm::runtime_query(selector);
  CHECK(result.ok());
  return ByteSize(result.value*4096);
}
}
int SysInfo::NumberOfProcessors() {
  long count=sysconf(_SC_NPROCESSORS_CONF);
  CHECK(count>0);
  return static_cast<int>(count);
}
int SysInfo::NumberOfEfficientProcessorsImpl() {
  // No heterogeneous-core topology is exported. Zero means unknown here.
  return 0;
}
ByteSize SysInfo::AmountOfTotalPhysicalMemoryImpl() { return FrameCount(5); }
ByteSize SysInfo::AmountOfAvailablePhysicalMemoryImpl() { return FrameCount(6); }
ByteSize SysInfo::AmountOfVirtualMemory() {
  // This is the native user virtual-address ceiling, not free/committed RAM.
  return ByteSize(abi::syscall::kUserTop);
}
std::optional<SysInfo::DiskSpaceInfo> SysInfo::AmountOfDiskSpace(const FilePath&) {
  // Native filesystem statistics are not exported; unknown is not zero space.
  return std::nullopt;
}
std::optional<int64_t> SysInfo::AmountOfFreeDiskSpace(const FilePath& path) {
  return AmountOfDiskSpace(path).transform([](DiskSpaceInfo information) {
    return static_cast<int64_t>(information.available.InBytes());
  });
}
std::optional<int64_t> SysInfo::AmountOfTotalDiskSpace(const FilePath& path) {
  return AmountOfDiskSpace(path).transform([](DiskSpaceInfo information) {
    return static_cast<int64_t>(information.total.InBytes());
  });
}
std::string SysInfo::OperatingSystemName() { return NativeIdentity().sysname; }
std::string SysInfo::OperatingSystemVersion() { return NativeIdentity().release; }
std::string SysInfo::OperatingSystemArchitecture() { return NativeIdentity().machine; }
void SysInfo::OperatingSystemVersionNumbers(int32_t* major,int32_t* minor,int32_t* bugfix) {
  auto version=charm::abi_version();
  CHECK(version.ok());
  *major=static_cast<int32_t>(version.value>>16);
  *minor=static_cast<int32_t>(version.value&0xffff);
  *bugfix=0;  // Native release numbers describe the syscall ABI; no patch field.
}
SysInfo::KernelVersionNumber SysInfo::KernelVersionNumber::Current() {
  KernelVersionNumber version;
  SysInfo::OperatingSystemVersionNumbers(&version.major,&version.minor,&version.bugfix);
  return version;
}
std::ostream& operator<<(std::ostream& output,const SysInfo::KernelVersionNumber& version) {
  return output<<version.major<<'.'<<version.minor<<'.'<<version.bugfix;
}
size_t SysInfo::VMAllocationGranularity() { return 4096; }
std::string SysInfo::CPUModelName() { return CPU().cpu_brand(); }
SysInfo::HardwareInfo SysInfo::GetHardwareInfoSync() {
  // The native API exposes no manufacturer/model identity; empty means unknown.
  return {};
}
std::optional<DriveInfo> GetFileDriveInfo(const FilePath&) { return std::nullopt; }
}  // namespace base
