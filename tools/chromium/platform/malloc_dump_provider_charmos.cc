#include "base/trace_event/malloc_dump_provider.h"
#include "base/trace_event/process_memory_dump.h"
#include "base/strings/stringprintf.h"
#include "partition_alloc/buildflags.h"
#include <charm/libc.h>
#include <cinttypes>
#include <string>

namespace base::trace_event {
const char MallocDumpProvider::kAllocatedObjects[]="malloc/allocated_objects";
MallocDumpProvider* MallocDumpProvider::GetInstance() {
  return Singleton<MallocDumpProvider,LeakySingletonTraits<MallocDumpProvider>>::get();
}
MallocDumpProvider::MallocDumpProvider()=default;
MallocDumpProvider::~MallocDumpProvider()=default;
bool MallocDumpProvider::OnMemoryDump(const MemoryDumpArgs&,ProcessMemoryDump* dump) {
  {
    AutoLock guard(emit_metrics_on_memory_dump_lock_);
    if(!emit_metrics_on_memory_dump_) return true;
  }
  if(!dump) return false;
  size_t live_capacity=charm_heap_allocated_bytes();
  auto* allocation=dump->CreateAllocatorDump(kAllocatedObjects);
  // Payload, RSS, free-list retention and total mapped bytes are distinct
  // quantities. Publish only the native counter that is actually measured.
  allocation->AddScalar("live_block_capacity",MemoryAllocatorDump::kUnitsBytes,live_capacity);
  return true;
}

#if PA_BUILDFLAG(USE_PARTITION_ALLOC)
namespace {
std::string PartitionDumpName(const char* root_name, const char* partition_name) {
  return base::StringPrintf("%s/%s/%s", root_name,
                            MemoryDumpPartitionStatsDumper::kPartitionsDumpName,
                            partition_name);
}
}  // namespace

MemoryDumpPartitionStatsDumper::MemoryDumpPartitionStatsDumper(
    const char* root_name,
    ProcessMemoryDump* memory_dump,
    MemoryDumpLevelOfDetail level_of_detail)
    : root_name_(root_name),
      memory_dump_(memory_dump),
      detailed_(level_of_detail != MemoryDumpLevelOfDetail::kBackground) {
  (void)detailed_;
}

void MemoryDumpPartitionStatsDumper::PartitionDumpTotals(
    const char* partition_name,
    const partition_alloc::PartitionMemoryStats* memory_stats) {
  total_mmapped_bytes_ += memory_stats->total_mmapped_bytes;
  total_resident_bytes_ += memory_stats->total_resident_bytes;
  total_active_bytes_ += memory_stats->total_active_bytes;
  total_active_count_ += memory_stats->total_active_count;
  syscall_count_ += memory_stats->syscall_count;
#if PA_BUILDFLAG(ENABLE_BACKUP_REF_PTR_SUPPORT)
  cumulative_brp_quarantined_bytes_ +=
      memory_stats->cumulative_brp_quarantined_bytes;
  cumulative_brp_quarantined_count_ +=
      memory_stats->cumulative_brp_quarantined_count;
#endif

  MemoryAllocatorDump* dump =
      memory_dump_->CreateAllocatorDump(PartitionDumpName(root_name_, partition_name));
  dump->AddScalar(MemoryAllocatorDump::kNameSize,
                  MemoryAllocatorDump::kUnitsBytes,
                  memory_stats->total_resident_bytes);
  dump->AddScalar("allocated_objects_size", MemoryAllocatorDump::kUnitsBytes,
                  memory_stats->total_active_bytes);
  dump->AddScalar("allocated_objects_count", "count",
                  memory_stats->total_active_count);
  dump->AddScalar("virtual_size", MemoryAllocatorDump::kUnitsBytes,
                  memory_stats->total_mmapped_bytes);
  dump->AddScalar("virtual_committed_size", MemoryAllocatorDump::kUnitsBytes,
                  memory_stats->total_committed_bytes);
  dump->AddScalar("allocated_size", MemoryAllocatorDump::kUnitsBytes,
                  memory_stats->total_allocated_bytes);
  dump->AddScalar("decommittable_size", MemoryAllocatorDump::kUnitsBytes,
                  memory_stats->total_decommittable_bytes);
  dump->AddScalar("discardable_size", MemoryAllocatorDump::kUnitsBytes,
                  memory_stats->total_discardable_bytes);
#if PA_BUILDFLAG(ENABLE_BACKUP_REF_PTR_SUPPORT)
  dump->AddScalar("brp_quarantined_size", MemoryAllocatorDump::kUnitsBytes,
                  memory_stats->total_brp_quarantined_bytes);
  dump->AddScalar("brp_quarantined_count", "count",
                  memory_stats->total_brp_quarantined_count);
#endif
  dump->AddScalar("syscall_count", "count", memory_stats->syscall_count);
  dump->AddScalar("intended_leak_size", MemoryAllocatorDump::kUnitsBytes,
                  memory_stats->total_intended_leak_bytes);
}

void MemoryDumpPartitionStatsDumper::PartitionsDumpBucketStats(
    const char* partition_name,
    const partition_alloc::PartitionBucketMemoryStats* memory_stats) {
  DCHECK(memory_stats->is_valid);
  std::string dump_name = PartitionDumpName(root_name_, partition_name);
  if (memory_stats->is_direct_map) {
    dump_name.append(
        base::StringPrintf("/buckets/directMap_%" PRIu64, ++uid_));
  } else {
    dump_name.append(base::StringPrintf("/buckets/bucket_%07" PRIu32,
                                        memory_stats->bucket_slot_size));
  }
  MemoryAllocatorDump* dump = memory_dump_->CreateAllocatorDump(dump_name);
  dump->AddScalar(MemoryAllocatorDump::kNameSize,
                  MemoryAllocatorDump::kUnitsBytes,
                  memory_stats->resident_bytes);
  dump->AddScalar("allocated_objects_size", MemoryAllocatorDump::kUnitsBytes,
                  memory_stats->active_bytes);
  dump->AddScalar("slot_size", MemoryAllocatorDump::kUnitsBytes,
                  memory_stats->bucket_slot_size);
  dump->AddScalar("decommittable_size", MemoryAllocatorDump::kUnitsBytes,
                  memory_stats->decommittable_bytes);
  dump->AddScalar("discardable_size", MemoryAllocatorDump::kUnitsBytes,
                  memory_stats->discardable_bytes);
}

void MemoryDumpPartitionStatsDumper::DumpIntendedLeak(uint32_t type_id,
                                                      size_t size) {
  std::string dump_name = base::StringPrintf(
      "%s/%s/leaked/LeakedSecurityObject/%08x", root_name_,
      MemoryDumpPartitionStatsDumper::kPartitionsDumpName, type_id);
  MemoryAllocatorDump* dump = memory_dump_->CreateAllocatorDump(dump_name);
  dump->AddScalar(MemoryAllocatorDump::kNameSize,
                  MemoryAllocatorDump::kUnitsBytes, size);
}
#endif  // PA_BUILDFLAG(USE_PARTITION_ALLOC)
} // namespace base::trace_event
