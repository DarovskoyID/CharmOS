#include "base/memory/platform_shared_memory_region.h"
#include "base/memory/shared_memory_mapper.h"
#include "base/memory/shared_memory_switch.h"
#include "base/memory/read_only_shared_memory_region.h"
#include "base/memory/unsafe_shared_memory_region.h"
#include "base/command_line.h"
#include "base/process/launch.h"
#include "base/process/kill.h"
#include "base/threading/platform_thread.h"
#include "base/system/sys_info.h"
#include <charm/process.hpp>
#include <errno.h>
#include <utility>

int TestNativeSharedMemoryChild() {
  auto* command=base::CommandLine::ForCurrentProcess();
  auto serialized=command->GetSwitchValueASCII("native-memory");
  auto readonly=base::shared_memory::ReadOnlySharedMemoryRegionFrom(serialized);
  if(!readonly.has_value()) return 60;
  auto permissions=charm::handle_rights(readonly->GetPlatformHandle());
  if(!permissions.ok() || (permissions.value&charm::rights::kWrite)) return 61;
  auto read_mapping=readonly->Map();
  if(!read_mapping.IsValid() || static_cast<const unsigned char*>(read_mapping.memory())[0]!=71) return 62;
  if(base::shared_memory::ReadOnlySharedMemoryRegionFrom(serialized).has_value()) return 63;
  auto writable=base::shared_memory::UnsafeSharedMemoryRegionFrom(
      command->GetSwitchValueASCII("native-writable-memory"));
  if(!writable.has_value()) return 64;
  auto write_mapping=writable->Map();
  if(!write_mapping.IsValid() || static_cast<unsigned char*>(write_mapping.memory())[0]!=72) return 65;
  static_cast<unsigned char*>(write_mapping.memory())[0]=92;
  if(base::shared_memory::ReadOnlySharedMemoryRegionFrom("4294967296,i,1,2,4096").has_value()) return 66;
  return 49;
}

int TestNativeSharedMemoryAndProcesses() {
  using Region=base::subtle::PlatformSharedMemoryRegion;
  auto memory=Region::CreateWritable(12288);
  if(!memory.IsValid() || memory.GetSize()!=12288) return 30;
  auto* mapper=base::SharedMemoryMapper::GetDefaultInstance();
  auto writable=memory.MapAt(0,12288,mapper);
  if(!writable) return 31;
  (*writable)[0]=17; (*writable)[4096]=33; (*writable)[8192]=57;
  if(!memory.ConvertToReadOnly()) return 32;
  auto permission=charm::handle_rights(memory.GetPlatformHandle());
  if(!permission.ok() || (permission.value&charm::rights::kWrite)) return 33;
  auto copied=memory.Duplicate();
  auto second=copied.MapAt(4096,4096,mapper);
  if(!second || (*second)[0]!=33) return 34;
  auto unaligned=memory.MapAt(4097,37,mapper);
  if(!unaligned || unaligned->size()!=37 || (*unaligned)[0]!=0) return 35;
  Region::Unmap(*unaligned,mapper);
  auto guid=memory.GetGUID();
  auto imported=Region::TakeOrFail(copied.PassPlatformHandle(),Region::Mode::kReadOnly,12288,guid);
  if(!imported.has_value() || !imported->IsValid()) return 36;
  auto wrong_size=imported->Duplicate();
  auto rejected=Region::TakeOrFail(wrong_size.PassPlatformHandle(),Region::Mode::kReadOnly,4096,guid);
  if(rejected.has_value()) return 37;
  memory=Region(); imported.value()=Region();
  if((*writable)[8192]!=57 || (*second)[0]!=33) return 38;
  Region::Unmap(*second,mapper); Region::Unmap(*writable,mapper);

  auto unsafe=Region::CreateUnsafe(4096);
  auto another=unsafe.Duplicate();
  auto first_map=unsafe.MapAt(0,4096,mapper),second_map=another.MapAt(0,4096,mapper);
  if(!first_map || !second_map) return 39;
  (*first_map)[0]=91;
  if((*second_map)[0]!=91) return 40;
  Region::Unmap(*first_map,mapper); Region::Unmap(*second_map,mapper);
  if(base::SysInfo::OperatingSystemName()!="CharmOS" || base::SysInfo::NumberOfProcessors()<=0 ||
     base::SysInfo::AmountOfTotalPhysicalMemory().InBytes()==0) return 41;

  base::LaunchOptions options;
  options.clear_environment=true;
  auto child=base::LaunchProcess(std::vector<std::string>{"/bin/base_runtime","native-child"},options);
  if(!child.IsValid() || child.Pid()==base::GetCurrentProcId()) return 42;
  auto duplicate=child.Duplicate();
  if(!duplicate.IsValid() || duplicate.Pid()!=child.Pid()) return 43;
  int code=0;
  if(!child.WaitForExitWithTimeout(base::Seconds(5),&code) || code!=37) return 44;
  int repeated_code=0;
  if(base::GetTerminationStatus(duplicate.Handle(),&repeated_code)!=base::TERMINATION_STATUS_ABNORMAL_TERMINATION || repeated_code!=37) return 45;
  if(!duplicate.WaitForExitWithTimeout(base::TimeDelta(),&repeated_code) || repeated_code!=37) return 46;
  errno=0;
  if(base::Process::Open(child.Pid()).IsValid() || errno!=EPERM) return 47;
  auto current=base::Process::Current().Duplicate();
  if(!current.is_current() || current.Pid()!=base::GetCurrentProcId()) return 48;

  auto readonly=base::ReadOnlySharedMemoryRegion::Create(4096);
  auto writable_region=base::UnsafeSharedMemoryRegion::Create(4096);
  if(!readonly.IsValid() || !writable_region.IsValid()) return 50;
  auto write_mapping=writable_region.Map();
  if(!write_mapping.IsValid()) return 51;
  static_cast<unsigned char*>(readonly.mapping.memory())[0]=71;
  static_cast<unsigned char*>(write_mapping.memory())[0]=72;
  base::CommandLine command(base::FilePath("/bin/base_runtime"));
  base::shared_memory::SharedMemorySwitch read_switch("native-memory",0,0);
  base::shared_memory::SharedMemorySwitch write_switch("native-writable-memory",0,0);
  base::LaunchOptions memory_options;
  memory_options.clear_environment=true;
  read_switch.AddToLaunchParameters(readonly.region,&command,&memory_options);
  write_switch.AddToLaunchParameters(writable_region,&command,&memory_options);
  if(memory_options.charmos_owned_handles.size()!=2 ||
     memory_options.charmos_handles[0].role==memory_options.charmos_handles[1].role) return 52;
  readonly.region=base::ReadOnlySharedMemoryRegion();
  writable_region=base::UnsafeSharedMemoryRegion();
  auto retained_options=memory_options;
  memory_options=base::LaunchOptions();
  auto memory_child=base::LaunchProcess(command,retained_options);
  int memory_exit=0;
  if(!memory_child.IsValid() || !memory_child.WaitForExitWithTimeout(base::Seconds(5),&memory_exit) || memory_exit!=49) return 53;
  if(static_cast<unsigned char*>(write_mapping.memory())[0]!=92) return 54;
  return 0;
}
