#include "base/process/memory.h"
#include <stdlib.h>

namespace base {
bool UncheckedMalloc(size_t size,void** result) {
  *result=malloc(size);
  return *result!=nullptr;
}
bool UncheckedCalloc(size_t count,size_t size,void** result) {
  *result=calloc(count,size);
  return *result!=nullptr;
}
void UncheckedFree(void* allocation) { free(allocation); }
}  // namespace base
