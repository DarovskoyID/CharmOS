#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/process/process_metrics.h"
#include "base/process/memory.h"
#include <charm/libc.h>
#include <array>
#include <errno.h>

int TestNativeFilesAndAccounting() {
  void* unchecked=nullptr;
  if(!base::UncheckedMalloc(73,&unchecked) || !unchecked) return 80;
  base::UncheckedFree(unchecked);
  if(!base::UncheckedCalloc(32,4,&unchecked) || !unchecked) return 81;
  bool zero=true;
  for(unsigned i=0;i<128;++i) if(static_cast<unsigned char*>(unchecked)[i]) zero=false;
  base::UncheckedFree(unchecked);
  if(!zero) return 82;
  unchecked=reinterpret_cast<void*>(1);
  if(base::UncheckedCalloc(SIZE_MAX,2,&unchecked) || unchecked) return 83;
  base::UncheckedFree(nullptr);
  const base::FilePath allocation_path("/base-allocation.bin");
  base::File allocation(allocation_path,base::File::FLAG_CREATE_ALWAYS|
      base::File::FLAG_READ|base::File::FLAG_WRITE);
  if(!allocation.IsValid()) return 70;
  std::array<uint8_t,8192> contents{},back{};
  for(size_t i=0;i<contents.size();++i) contents[i]=static_cast<uint8_t>(i%251);
  if(!allocation.WriteAndCheck(0,contents) || allocation.Seek(base::File::FROM_BEGIN,17)!=17) return 71;
  if(!base::AllocateFileRegion(&allocation,1024,12000) || allocation.GetLength()!=13024 ||
     allocation.Seek(base::File::FROM_CURRENT,0)!=17) return 72;
  if(!allocation.ReadAndCheck(0,back) || contents!=back) return 73;
  std::array<uint8_t,4832> extension{};
  extension.fill(255);
  if(!allocation.ReadAndCheck(8192,extension)) return 74;
  for(uint8_t byte:extension) if(byte) return 75;
  errno=0;
  if(base::AllocateFileRegion(&allocation,-1,4096) || errno!=EINVAL) return 76;
  allocation.Close();
  if(!base::DeleteFile(allocation_path)) return 77;

  auto metrics=base::ProcessMetrics::CreateCurrentProcessMetrics();
  if(!metrics) return 78;
  auto cpu=metrics->GetCumulativeCPUUsage();
  if(!cpu.has_value() || cpu->is_negative()) return 79;
  if(metrics->GetOpenFdCount()!=charm_open_fd_count() ||
     metrics->GetOpenFdSoftLimit()<=0 || metrics->GetMallocUsage()!=charm_heap_allocated_bytes()) return 80;
  errno=0;
  if(metrics->GetMemoryInfo().has_value() || errno!=ENOTSUP) return 81;
  return 0;
}
