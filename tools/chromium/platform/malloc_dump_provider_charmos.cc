#include "base/trace_event/malloc_dump_provider.h"
#include "base/trace_event/process_memory_dump.h"
#include <charm/libc.h>

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
} // namespace base::trace_event
