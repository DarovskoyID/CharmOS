#include "base/memory/platform_shared_memory_mapper.h"
#include "base/check.h"
#include "base/compiler_specific.h"
#include "base/no_destructor.h"
#include "base/synchronization/lock.h"
#include <charm/memory.hpp>
#include <new>
#include <utility>

namespace base {
namespace {
struct MappingRecord {
  uint8_t* view;
  size_t size;
  charm::Mapping allocation;
  MappingRecord* next;
};
struct MappingRegistry { Lock lock; MappingRecord* head=nullptr; };
MappingRegistry& Registry() { static NoDestructor<MappingRegistry> registry; return *registry; }
}
std::optional<span<uint8_t>> PlatformSharedMemoryMapper::Map(
    subtle::PlatformSharedMemoryHandle handle,bool write_allowed,uint64_t offset,size_t size) {
  if(!size || (offset&4095)) return std::nullopt;
  uint32_t rights=charm::rights::kRead|charm::rights::kMap;
  if(write_allowed) rights|=charm::rights::kWrite;
  auto duplicate=charm::handle_duplicate(handle,rights);
  if(!duplicate.ok()) return std::nullopt;
  auto owner=charm::UniqueHandle::adopt(static_cast<charm::Handle>(duplicate.value));
  charm::SharedMemory memory;
  if(charm::SharedMemory::adopt(std::move(owner),&memory)!=charm::Status::Ok ||
     offset>memory.size() || size>memory.size()-offset) return std::nullopt;
  charm::Mapping mapping;
  if(memory.map(write_allowed ? charm::MemoryAccess::ReadWrite : charm::MemoryAccess::ReadOnly,&mapping)!=charm::Status::Ok)
    return std::nullopt;
  // The native ABI maps whole VMOs. Return the requested view while retaining
  // the real mapping base for destruction, including nonzero file offsets.
  auto* view=UNSAFE_TODO(static_cast<uint8_t*>(mapping.data())+offset);
  auto* record=new(std::nothrow) MappingRecord{view,size,std::move(mapping),nullptr};
  if(!record) return std::nullopt;
  {
    auto& registry=Registry();
    AutoLock guard(registry.lock);
    record->next=registry.head; registry.head=record;
  }
  return UNSAFE_TODO(span(view,size));
}
void PlatformSharedMemoryMapper::Unmap(span<uint8_t> mapping) {
  MappingRecord* removed=nullptr;
  {
    auto& registry=Registry();
    AutoLock guard(registry.lock);
    for(auto** link=&registry.head;*link;link=&(*link)->next) {
      auto* record=*link;
      auto begin=reinterpret_cast<uintptr_t>(record->view);
      auto requested=reinterpret_cast<uintptr_t>(mapping.data());
      // Region::MapAt may trim a leading subpage alignment adjustment. Both
      // the adjusted and original view retain the same end address.
      if(requested>=begin && requested-begin<record->size &&
         mapping.size()==record->size-(requested-begin)) {
        *link=record->next; removed=record; break;
      }
    }
  }
  CHECK(removed);
  delete removed;
}
}  // namespace base
