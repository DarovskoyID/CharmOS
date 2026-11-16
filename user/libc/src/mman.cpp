#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "internal.hpp"

namespace {
constexpr size_t page=4096;
// Lazy PROT_NONE reservations back allocator VA pools and are never committed
// up front; the public VMO limit still applies to ordinary mappings.
constexpr size_t kLazyMapMaxSize=64ull<<30;
// One byte per touched page holds protection, known-zero and live bits.
// Untouched ranges use a default state, without eager metadata for huge pools.
constexpr unsigned char kProtection = PROT_READ | PROT_WRITE | PROT_EXEC;
constexpr unsigned char kZero = 8, kLive = 16;
constexpr size_t kStateChunk = 4096;
struct StateChunk { unsigned char* bytes; unsigned char uniform; };
struct Mapping {
  void* address;
  size_t size;
  StateChunk* state;
  Mapping* next;
};
unsigned char state_at(const Mapping* mapping, size_t index) {
  const auto& chunk = mapping->state[index / kStateChunk];
  return chunk.bytes ? chunk.bytes[index % kStateChunk] : chunk.uniform;
}
// Preflight metadata before changing kernel mappings. Allocation failure may
// leave unused chunks owned by the record, but never partially changes rights.
bool prepare_state(Mapping* mapping, size_t first, size_t count, bool edges_only=false) {
  size_t end = (first + count + kStateChunk - 1) / kStateChunk;
  for (size_t i = first / kStateChunk; i < end; ++i) {
    if (edges_only && first<=i*kStateChunk && first+count>=(i+1)*kStateChunk) continue;
    if (mapping->state[i].bytes) continue;
    auto* chunk = static_cast<unsigned char*>(malloc(kStateChunk));
    if (!chunk) return false;
    memset(chunk, mapping->state[i].uniform, kStateChunk);
    mapping->state[i].bytes = chunk;
  }
  return true;
}
unsigned char& writable_state(Mapping* mapping, size_t index) {
  return mapping->state[index / kStateChunk].bytes[index % kStateChunk];
}
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
// Caller holds mappings_lock. Keep kernel permissions and per-page metadata
// together, including when replacing a private reservation at a fixed address.
int protect_locked(Mapping* record,void* address,size_t rounded,int protection) {
  size_t first=(reinterpret_cast<uintptr_t>(address)-reinterpret_cast<uintptr_t>(record->address))/page;
  // POSIX mprotect on an unmapped range fails; never apply it partially.
  for (size_t i=0;i<rounded/page;++i)
    if (!(state_at(record,first+i) & kLive)) return libc::fail_errno(ENOMEM);
  bool unchanged=true;
  for (size_t i=0;i<rounded/page;++i)
    if ((state_at(record,first+i) & kProtection)!=protection) unchanged=false;
  if (unchanged) return 0;
  if (!prepare_state(record,first,rounded/page)) return libc::fail_errno(ENOMEM);
  uint32_t rights=protection==PROT_NONE ? 0 : charm::rights::kRead;
  if (protection&PROT_WRITE) rights|=charm::rights::kWrite;
  if (protection&PROT_EXEC) rights|=charm::rights::kExec;
  auto status=charm::vmo_protect(reinterpret_cast<uint64_t>(address),rounded,rights);
  if (status!=charm::Status::Ok) return libc::fail(status);
  for (size_t i=0;i<rounded/page;++i) {
    writable_state(record,first+i) = static_cast<unsigned char>((state_at(record,first+i) & ~kProtection) | protection);
    // Once writable, the page can no longer be assumed zero.
    if (protection&PROT_WRITE) writable_state(record,first+i) &= static_cast<unsigned char>(~kZero);
  }
  return 0;
}
}
extern "C" void* mmap(void* hint,size_t size,int protection,int flags,int fd,off_t offset) {
  if (!size || offset<0 || (static_cast<uint64_t>(offset)&(page-1)) || (protection&~(PROT_READ|PROT_WRITE|PROT_EXEC)) ||
      (flags&~(MAP_SHARED|MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS|MAP_NORESERVE))) return fail_map(EINVAL);
  int sharing=flags&(MAP_PRIVATE|MAP_SHARED);
  if (!sharing || sharing==(MAP_PRIVATE|MAP_SHARED)) return fail_map(EINVAL);
  if (size>SIZE_MAX-(page-1)) return fail_map(ENOMEM);
  // Private anonymous replacement inside an existing private mapping. Pages
  // that are still known-zero are only denied (no commit). Dirty pages are made
  // writable, zeroed, then denied again. This keeps a large untouched lazy
  // reservation untouched when V8 unmaps a few holes.
  if (flags&MAP_FIXED) {
    if (!range(hint,size)) return fail_map(EINVAL);
    if (protection!=PROT_NONE || !(flags&MAP_ANONYMOUS) || sharing!=MAP_PRIVATE)
      return fail_map(ENOTSUP);
    if (offset) return fail_map(EINVAL);
    size_t rounded=(size+page-1)&~(page-1);
    libc::Guard guard(mappings_lock);
    auto* record=find_range(hint,rounded);
    if (!record) return fail_map(ENOMEM);
    size_t first=(reinterpret_cast<uintptr_t>(hint)-reinterpret_cast<uintptr_t>(record->address))/page;
    size_t count=rounded/page;
    if (!prepare_state(record,first,count)) return fail_map(ENOMEM);
    if (protect_locked(record,hint,rounded,PROT_NONE)!=0) return MAP_FAILED;
    for (size_t i=0;i<count;) {
      if (state_at(record,first+i) & kZero) { ++i; continue; }
      size_t run=i+1;
      while (run<count && !(state_at(record,first+run) & kZero)) ++run;
      auto* start=static_cast<unsigned char*>(hint)+i*page;
      size_t length=(run-i)*page;
      if (protect_locked(record,start,length,PROT_READ|PROT_WRITE)!=0) return MAP_FAILED;
      memset(start,0,length);
      if (protect_locked(record,start,length,PROT_NONE)!=0) return MAP_FAILED;
      for (size_t k=i;k<run;++k) writable_state(record,first+k) |= kZero;
      i=run;
    }
    for (size_t i=0;i<count;++i) writable_state(record,first+i) |= kZero;
    return hint;
  }
  // PROT_NONE uses non-present VMO leaves (ABI v1.20), not a readable alias.
  if ((flags&(MAP_PRIVATE|MAP_SHARED))!=MAP_PRIVATE ||
      (protection!=PROT_NONE && !(protection&PROT_READ))) return fail_map(ENOTSUP);
  // W^X: страница не бывает одновременно записываемой и исполняемой.
  if ((protection&PROT_WRITE) && (protection&PROT_EXEC)) return fail_map(EINVAL);
  const bool lazy=(flags&MAP_ANONYMOUS) && protection==PROT_NONE;
  if (size>(lazy ? kLazyMapMaxSize : abi::syscall::kVmoMaxSize)) return fail_map(ENOMEM);
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
  size_t chunks=(rounded/page+kStateChunk-1)/kStateChunk;
  auto* states=static_cast<StateChunk*>(calloc(chunks,sizeof(StateChunk)));
  if (!states) {
    free(record);
    return fail_map(ENOMEM);
  }
  // Anonymous PROT_NONE is a lazy reservation: VmoReserve keeps zero frame
  // entries, and access/protection changes commit only the touched pages.
  auto object=lazy ? charm::vmo_reserve(rounded) : charm::vmo_create(rounded);
  if (!object.ok()) { free(states); free(record); return fail_map(libc::errno_from(object.status)); }
  auto handle=static_cast<charm::Handle>(object.value);
  uint32_t rights=protection==PROT_NONE ? 0 : charm::rights::kRead;
  if (!anonymous) rights=charm::rights::kRead|charm::rights::kWrite;
  else if (protection&PROT_WRITE) rights|=charm::rights::kWrite;
  if (anonymous && (protection&PROT_EXEC)) rights|=charm::rights::kExec;
  auto mapped=charm::vmo_map(handle,rights);
  if (!mapped.ok()) {
    (void)charm::handle_close(handle); free(states); free(record);
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
        uint32_t final_rights=protection==PROT_NONE ? 0 : charm::rights::kRead;
        if (protection&PROT_EXEC) final_rights|=charm::rights::kExec;
        mapped=charm::vmo_map(handle,final_rights);
        if (!mapped.ok()) error=libc::errno_from(mapped.status);
        address=mapped.ok() ? reinterpret_cast<unsigned char*>(mapped.value) : nullptr;
      }
    }
  }
  (void)charm::handle_close(handle);
  if (error) {
    if (address) (void)charm::vmo_unmap(reinterpret_cast<uint64_t>(address));
    free(states); free(record); return fail_map(error);
  }
  for (size_t i=0;i<chunks;++i)
    states[i].uniform=static_cast<unsigned char>(protection | (lazy ? kZero : 0) | kLive);
  *record={address,rounded,states,nullptr};
  { libc::Guard guard(mappings_lock); record->next=mappings; mappings=record; }
  return address;
}
extern "C" int munmap(void* address,size_t size) {
  if (!range(address,size) || size>SIZE_MAX-(page-1)) return libc::fail_errno(EINVAL);
  size_t rounded=(size+page-1)&~(page-1);
  Mapping* removed=nullptr;
  {
    libc::Guard guard(mappings_lock);
    Mapping* record=find_range(address,rounded);
    if (!record) return libc::fail_errno(EINVAL);
    size_t first=(reinterpret_cast<uintptr_t>(address)-reinterpret_cast<uintptr_t>(record->address))/page;
    size_t count=rounded/page;
    if (record->address!=address || record->size!=rounded) {
      // POSIX splits the mapping. Remove just these leaves; the VMO frames stay
      // owned by the VMO (discard/reclaim is a separate operation) and the
      // record keeps the surrounding range usable.
      if (!prepare_state(record,first,count,true)) return libc::fail_errno(ENOMEM);
      auto status=charm::vmo_unmap_range(reinterpret_cast<uint64_t>(address),rounded);
      if (status!=charm::Status::Ok) return libc::fail(status);
      for (size_t i=first;i<first+count;) {
        auto& chunk=record->state[i/kStateChunk];
        if (i%kStateChunk==0 && first+count-i>=kStateChunk) {
          free(chunk.bytes); chunk.bytes=nullptr;
          chunk.uniform=static_cast<unsigned char>(chunk.uniform & ~kLive);
          i+=kStateChunk;
        } else {
          writable_state(record,i) &= static_cast<unsigned char>(~kLive);
          ++i;
        }
      }
      return 0;
    }
    auto status=charm::vmo_unmap(reinterpret_cast<uint64_t>(address));
    if (status!=charm::Status::Ok) return libc::fail(status);
    Mapping** link=&mappings;
    while (*link && *link!=record) link=&(*link)->next;
    removed=record; *link=removed->next;
  }
  size_t chunks=(removed->size/page+kStateChunk-1)/kStateChunk;
  for (size_t i=0;i<chunks;++i) free(removed->state[i].bytes);
  free(removed->state);
  free(removed);
  return 0;
}
extern "C" int mprotect(void* address,size_t size,int protection) {
  if (!range(address,size) || size>SIZE_MAX-(page-1) ||
      (protection&~(PROT_READ|PROT_WRITE|PROT_EXEC))) return libc::fail_errno(EINVAL);
  if (protection!=PROT_NONE && !(protection&PROT_READ)) return libc::fail_errno(ENOTSUP);
  if ((protection&PROT_WRITE) && (protection&PROT_EXEC)) return libc::fail_errno(EINVAL);
  size_t rounded=(size+page-1)&~(page-1);
  libc::Guard guard(mappings_lock);
  auto* record=find_range(address,rounded);
  if (!record) return libc::fail_errno(ENOMEM);
  return protect_locked(record,address,rounded,protection);
}
extern "C" int madvise(void* address,size_t size,int advice) {
  if (!range(address,size)) return libc::fail_errno(EINVAL);
  libc::Guard guard(mappings_lock);
  if (!find_range(address,size)) return libc::fail_errno(ENOMEM);
  // CharmOS не имеет swap/demand paging: MADV_DONTNEED принимается как
  // подсказка (страницы остаются доступны), а не как ошибка. V8 вызывает его
  // при освобождении страниц и проверяет успех.
  if (advice==MADV_NORMAL || advice==MADV_DONTNEED) return 0;
  return libc::fail_errno(ENOTSUP);
}
extern "C" int msync(void* address,size_t size,int flags) {
  if (!range(address,size) || (flags&~(MS_SYNC|MS_ASYNC|MS_INVALIDATE)) ||
      ((flags&MS_SYNC) && (flags&MS_ASYNC))) return libc::fail_errno(EINVAL);
  libc::Guard guard(mappings_lock);
  return find_range(address,size) ? 0 : libc::fail_errno(ENOMEM);
}

extern "C" int mlock(const void* address,size_t size) {
  if (!address && size) return libc::fail_errno(ENOMEM);
  // All CharmOS user pages are resident once committed; nothing to pin.
  return 0;
}
extern "C" int munlock(const void* address,size_t size) {
  if (!address && size) return libc::fail_errno(ENOMEM);
  return 0;
}
extern "C" int mlockall(int) { return 0; }
extern "C" int munlockall(void) { return 0; }
