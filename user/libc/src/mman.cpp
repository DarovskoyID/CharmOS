#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "internal.hpp"

namespace {
constexpr size_t page=4096;
struct Mapping { void* address; size_t size; int protection; Mapping* next; };
libc::Lock mappings_lock;
Mapping* mappings;

bool range(void* address,size_t size) {
  if ((reinterpret_cast<uintptr_t>(address)&(page-1)) || !size ||
      size>UINTPTR_MAX-reinterpret_cast<uintptr_t>(address)) return false;
  return true;
}
Mapping* find_range(void* address,size_t size) {
  uintptr_t start=reinterpret_cast<uintptr_t>(address);
  for (auto* mapping=mappings;mapping;mapping=mapping->next) {
    uintptr_t base=reinterpret_cast<uintptr_t>(mapping->address);
    if (start>=base && start-base<mapping->size && size<=mapping->size-(start-base)) return mapping;
  }
  return nullptr;
}
void* fail_map(int error) { errno=error; return MAP_FAILED; }
}
extern "C" void* mmap(void*,size_t size,int protection,int flags,int fd,off_t offset) {
  if (!size || offset<0 || (static_cast<uint64_t>(offset)&(page-1)) || (protection&~(PROT_READ|PROT_WRITE|PROT_EXEC)) ||
      (flags&~(MAP_SHARED|MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS))) return fail_map(EINVAL);
  int sharing=flags&(MAP_PRIVATE|MAP_SHARED);
  if (!sharing || sharing==(MAP_PRIVATE|MAP_SHARED)) return fail_map(EINVAL);
  if ((flags&(MAP_PRIVATE|MAP_SHARED))!=MAP_PRIVATE || (flags&MAP_FIXED) ||
      !(protection&PROT_READ) || (protection&PROT_EXEC)) return fail_map(ENOTSUP);
  if (size>abi::syscall::kVmoMaxSize) return fail_map(ENOMEM);
  size_t rounded=(size+page-1)&~(page-1);
  bool anonymous=(flags&MAP_ANONYMOUS)!=0;
  if (anonymous && offset) return fail_map(EINVAL);
  libc::FdLease file(anonymous ? -1 : fd);
  if (!anonymous) {
    if (!file || !file->readable) return fail_map(EBADF);
    if (file->kind!=libc::Kind::File) return fail_map(ENODEV);
    abi::fs::Stat info;
    auto status=charm::fs_stat(file->handle,&info);
    if (status!=charm::Status::Ok) return fail_map(libc::errno_from(status));
    if (static_cast<uint64_t>(offset)>info.size || size>info.size-static_cast<uint64_t>(offset))
      return fail_map(EINVAL);
  }
  auto* record=static_cast<Mapping*>(malloc(sizeof(Mapping)));
  if (!record) return fail_map(ENOMEM);
  auto object=charm::vmo_create(rounded);
  if (!object.ok()) { free(record); return fail_map(libc::errno_from(object.status)); }
  auto handle=static_cast<charm::Handle>(object.value);
  uint32_t rights=charm::rights::kRead;
  if ((protection&PROT_WRITE) || !anonymous) rights|=charm::rights::kWrite;
  auto mapped=charm::vmo_map(handle,rights);
  if (!mapped.ok()) {
    (void)charm::handle_close(handle); free(record);
    return fail_map(libc::errno_from(mapped.status));
  }
  auto* address=reinterpret_cast<unsigned char*>(mapped.value);
  int error=0;
  if (!anonymous) {
    size_t read=0;
    while (read<size) {
      size_t amount=size-read;
      if (amount>abi::fs::kMaxTransfer) amount=abi::fs::kMaxTransfer;
      auto incoming=charm::file_read(file->handle,address+read,amount,static_cast<uint64_t>(offset)+read);
      if (!incoming.ok()) { error=libc::errno_from(incoming.status); break; }
      if (!incoming.value) { error=EIO; break; } // File shrank while snapshotting.
      read+=static_cast<size_t>(incoming.value);
    }
    if (!error && !(protection&PROT_WRITE)) {
      auto status=charm::vmo_unmap(mapped.value);
      if (status!=charm::Status::Ok) error=libc::errno_from(status);
      else {
        mapped=charm::vmo_map(handle,charm::rights::kRead);
        if (!mapped.ok()) error=libc::errno_from(mapped.status);
        address=mapped.ok() ? reinterpret_cast<unsigned char*>(mapped.value) : nullptr;
      }
    }
  }
  (void)charm::handle_close(handle);
  if (error) {
    if (address) (void)charm::vmo_unmap(reinterpret_cast<uint64_t>(address));
    free(record); return fail_map(error);
  }
  *record={address,rounded,protection,nullptr};
  { libc::Guard guard(mappings_lock); record->next=mappings; mappings=record; }
  return address;
}
extern "C" int munmap(void* address,size_t size) {
  if (!range(address,size) || size>SIZE_MAX-(page-1)) return libc::fail_errno(EINVAL);
  size_t rounded=(size+page-1)&~(page-1);
  Mapping* removed=nullptr;
  {
    libc::Guard guard(mappings_lock);
    Mapping** link=&mappings;
    while (*link && (*link)->address!=address) link=&(*link)->next;
    if (!*link) return libc::fail_errno(find_range(address,size) ? ENOTSUP : EINVAL);
    if ((*link)->size!=rounded) return libc::fail_errno(ENOTSUP);
    auto status=charm::vmo_unmap(reinterpret_cast<uint64_t>(address));
    if (status!=charm::Status::Ok) return libc::fail(status);
    removed=*link; *link=removed->next;
  }
  free(removed);
  return 0;
}
extern "C" int mprotect(void* address,size_t size,int protection) {
  if (!range(address,size) || (protection&~(PROT_READ|PROT_WRITE|PROT_EXEC))) return libc::fail_errno(EINVAL);
  libc::Guard guard(mappings_lock);
  auto* record=find_range(address,size);
  if (!record) return libc::fail_errno(ENOMEM);
  return record->protection==protection ? 0 : libc::fail_errno(ENOTSUP);
}
extern "C" int madvise(void* address,size_t size,int advice) {
  if (!range(address,size)) return libc::fail_errno(EINVAL);
  libc::Guard guard(mappings_lock);
  if (!find_range(address,size)) return libc::fail_errno(ENOMEM);
  return advice==MADV_NORMAL ? 0 : libc::fail_errno(ENOTSUP);
}
extern "C" int msync(void* address,size_t size,int flags) {
  if (!range(address,size) || (flags&~(MS_SYNC|MS_ASYNC|MS_INVALIDATE)) ||
      ((flags&MS_SYNC) && (flags&MS_ASYNC))) return libc::fail_errno(EINVAL);
  libc::Guard guard(mappings_lock);
  return find_range(address,size) ? 0 : libc::fail_errno(ENOMEM);
}
