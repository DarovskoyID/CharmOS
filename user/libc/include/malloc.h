#pragma once
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif
// Native allocator payload capacity; the pointer must be a live allocation.
size_t malloc_usable_size(void* allocation);
#ifdef __cplusplus
}
#endif
