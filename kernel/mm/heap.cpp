#include "heap.hpp"

#include "../core/panic.hpp"
#include "layout.hpp"
#include "vmm.hpp"

namespace kernel::mm {

namespace {

constinit lib::mm::Heap g_heap;

bool grow(void*, uintptr_t at, size_t bytes) {
  for (size_t off = 0; off < bytes; off += 4096) {
    if (map_new_page(at + off).is_err()) return false;
  }
  return true;
}

}  // namespace

void heap_init() {
  auto r = g_heap.init(kHeapBase, kHeapMax, &grow, nullptr);
  if (r.is_err()) panic("heap: init failed (error {})", r.error());
  kprint("heap: ready at {x}, limit {} MiB\n", kHeapBase, kHeapMax >> 20);
}

lib::core::Result<void*, HeapError> kmalloc(size_t size) { return g_heap.alloc(size); }

void kfree(void* ptr) {
  auto r = g_heap.free(ptr);
  if (r.is_err()) panic("heap: bad free of {} (error {})", ptr, r.error());
}

bool heap_check() { return g_heap.check(); }
size_t heap_used_bytes() { return g_heap.used_bytes(); }
size_t heap_mapped_bytes() { return g_heap.mapped_bytes(); }

}  // namespace kernel::mm
