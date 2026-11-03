// Native implementation of Perfetto's public PagedMemory interface.
// Fixed native ELFs live below the VMO arena. The kernel leaves one unmapped
// page between VMO mappings, so both bounds of each allocation are guarded.
#include "perfetto/ext/base/paged_memory.h"
#include <charm/memory.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <utility>

namespace perfetto::base {
namespace {
[[noreturn]] void memory_failure() {
  fputs("Perfetto: native VMO allocation/release failed\n",stderr);
  abort();
}
void release(char* address) {
  if (address && charm::vmo_unmap(reinterpret_cast<uint64_t>(address))!=charm::Status::Ok)
    memory_failure();
}
}
PagedMemory PagedMemory::Allocate(size_t size, int flags) {
  char* address=nullptr;
  if (size && size<=abi::syscall::kVmoMaxSize &&
      !(flags & ~(kMayFail|kDontCommit))) {
    charm::SharedMemory object;
    if (charm::SharedMemory::create(size,&object)==charm::Status::Ok) {
      charm::Mapping mapping;
      if (object.map(charm::MemoryAccess::ReadWrite,&mapping)==charm::Status::Ok)
        address=static_cast<char*>(mapping.release());
    }
  }
  if (!address) {
    if (flags&kMayFail) return PagedMemory();
    memory_failure();
  }
  return PagedMemory(address,size);
}
PagedMemory::PagedMemory() = default;
PagedMemory::PagedMemory(char* address,size_t size):p_(address),size_(size) {}
PagedMemory::PagedMemory(PagedMemory&& other) noexcept:p_(other.p_),size_(other.size_) {
  other.p_=nullptr;
  other.size_=0;
}
PagedMemory& PagedMemory::operator=(PagedMemory&& other) {
  if (this!=&other) {
    release(p_);
    p_=other.p_;
    size_=other.size_;
    other.p_=nullptr;
    other.size_=0;
  }
  return *this;
}
PagedMemory::~PagedMemory() { release(p_); }
bool PagedMemory::AdviseDontNeed(void*,size_t) {
  // Optional hint: the current VMO ABI cannot discard individual pages.
  return false;
}
#if TRACK_COMMITTED_SIZE()
void PagedMemory::EnsureCommitted(size_t size) {
  if (size>size_) memory_failure();
  // Native VMO pages are committed and zeroed at creation, including when
  // the caller permits deferred commitment with kDontCommit.
  committed_size_=size_;
}
#endif
}  // namespace perfetto::base
