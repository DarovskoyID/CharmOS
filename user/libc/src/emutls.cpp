#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "internal.hpp"

namespace {
// Pinned compiler-rt emutls control ABI, not a kernel/shared wire structure.
struct Control {
  uintptr_t size;
  uintptr_t alignment;
  uintptr_t runtime_index;
  const void* initial;
};
static_assert(sizeof(Control)==32 && offsetof(Control,alignment)==8);
static_assert(offsetof(Control,runtime_index)==16 && offsetof(Control,initial)==24);
struct Instance { const Control* control; void* storage; Instance* next; };
struct Destructor { void (*function)(void*); void* object; Destructor* next; };
}

extern "C" void* __emutls_get_address(Control* control) {
  auto* thread=libc::current_thread();
  for(auto* entry=static_cast<Instance*>(thread->compiler_tls);entry;entry=entry->next)
    if(entry->control==control) return entry->storage;
  size_t alignment=static_cast<size_t>(control->alignment);
  if(alignment<sizeof(void*)) alignment=sizeof(void*);
  if(alignment&(alignment-1)) abort();
  auto* entry=static_cast<Instance*>(malloc(sizeof(Instance)));
  if(!entry) abort();
  void* storage=nullptr;
  if(posix_memalign(&storage,alignment,static_cast<size_t>(control->size)) || !storage) {
    free(entry); abort();
  }
  if(control->initial) memcpy(storage,control->initial,static_cast<size_t>(control->size));
  else memset(storage,0,static_cast<size_t>(control->size));
  *entry={control,storage,static_cast<Instance*>(thread->compiler_tls)};
  thread->compiler_tls=entry;
  return storage;
}

extern "C" int __cxa_thread_atexit_impl(void (*function)(void*),void* object,void*) {
  if(!function) return -1;
  auto* entry=static_cast<Destructor*>(malloc(sizeof(Destructor)));
  if(!entry) abort();
  auto* thread=libc::current_thread();
  *entry={function,object,static_cast<Destructor*>(thread->compiler_tls_destructors)};
  thread->compiler_tls_destructors=entry;
  return 0;
}
extern "C" int __cxa_thread_atexit(void (*function)(void*),void* object,void* dso) {
  return __cxa_thread_atexit_impl(function,object,dso);
}

namespace libc {
void run_compiler_tls_destructors() {
  auto* thread=current_thread();
  while(auto* entry=static_cast<Destructor*>(thread->compiler_tls_destructors)) {
    thread->compiler_tls_destructors=entry->next;
    auto function=entry->function;
    void* object=entry->object;
    free(entry);
    function(object);
  }
}
void release_compiler_tls() {
  auto* thread=current_thread();
  while(auto* entry=static_cast<Instance*>(thread->compiler_tls)) {
    thread->compiler_tls=entry->next;
    free(entry->storage);
    free(entry);
  }
}
} // namespace libc
