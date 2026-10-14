#include "stack.hpp"

#include "../sync/spinlock.hpp"
#include "layout.hpp"

namespace kernel::mm {

namespace {

constinit uint64_t g_next = kStackBase;
constinit sync::Spinlock g_lock{sync::LockRank::MmStacks, "mm.stacks"};

}  // namespace

lib::core::Result<KernelStack, VmError> alloc_kernel_stack(size_t size) {
  using R = lib::core::Result<KernelStack, VmError>;
  size = (size + 4095) & ~size_t{4095};
  uint64_t slot;
  {
    sync::SpinGuard guard(g_lock);
    if (size + 4096 > kStackBase + kStackMax - g_next) return R::err(VmError::OutOfAddressSpace);
    slot = g_next;
    g_next += size + 4096;
  }
  uint64_t bottom = slot + 4096;  // страница slot остаётся неотображённой
  for (uint64_t off = 0; off < size; off += 4096) {
    auto r = map_new_page(bottom + off);
    if (r.is_err()) return R::err(r.error());
  }
  return R::ok({bottom, bottom + size});
}

}  // namespace kernel::mm
