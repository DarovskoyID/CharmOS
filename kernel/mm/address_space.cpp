#include "address_space.hpp"

#include <abi/syscall.hpp>

#include "layout.hpp"
#include "pmm.hpp"

namespace kernel::mm {

using lib::core::Status;

namespace {

uint64_t table_frame(void*) {
  auto r = alloc_frames();
  return r.is_ok() ? r.value() : 0;
}

void free_frame(void*, uint64_t phys) { free_frames(phys); }

}  // namespace

Status<VmError> AddressSpace::create() {
  if (!pt_.init(&table_frame, nullptr, kHhdmBase)) return Status<VmError>::err(VmError::OutOfMemory);
  auto* mine = phys_to_virt<uint64_t>(pt_.root());
  const auto* kernel = phys_to_virt<const uint64_t>(kernel_root());
  for (int i = 256; i < 512; ++i) mine[i] = kernel[i];
  return lib::core::ok_status<VmError>();
}

Status<VmError> AddressSpace::map_new(uint64_t virt, uint64_t flags, uint64_t* phys_out) {
  if (virt >= abi::syscall::kUserTop) return Status<VmError>::err(VmError::OutOfAddressSpace);
  auto frame = alloc_frames();
  if (frame.is_err()) return Status<VmError>::err(VmError::OutOfMemory);
  auto* p = phys_to_virt<uint64_t>(frame.value());
  for (size_t i = 0; i < 512; ++i) p[i] = 0;
  if (!pt_.map_4k(virt, frame.value(), flags | arch::x86_64::kMapUser)) {
    free_frames(frame.value());
    return Status<VmError>::err(VmError::AlreadyMapped);
  }
  if (phys_out != nullptr) *phys_out = frame.value();
  return lib::core::ok_status<VmError>();
}

Status<VmError> AddressSpace::map_phys(uint64_t virt, uint64_t phys, uint64_t flags) {
  if (virt >= abi::syscall::kUserTop) return Status<VmError>::err(VmError::OutOfAddressSpace);
  if (!pt_.map_4k(virt, phys, flags | arch::x86_64::kMapUser)) {
    return Status<VmError>::err(VmError::AlreadyMapped);
  }
  return lib::core::ok_status<VmError>();
}

lib::core::Result<AddressSpace::FrameRun, VmError> AddressSpace::map_new_run(
    uint64_t virt, uint64_t pages, uint64_t flags, bool zero) {
  using R = lib::core::Result<FrameRun, VmError>;
  constexpr uint64_t page = arch::x86_64::kPageSize;
  if (!pages || pages > 1024 || virt % page || virt >= abi::syscall::kUserTop ||
      pages > (abi::syscall::kUserTop - virt) / page) return R::err(VmError::OutOfAddressSpace);
  auto frame = alloc_frames_near_hint(pages);
  if (frame.is_err() && pages > 1) {
    pages = 1;
    frame = alloc_frames();
  }
  if (frame.is_err()) return R::err(VmError::OutOfMemory);
  if (zero) __builtin_memset(phys_to_virt<void>(frame.value()), 0, pages * page);
  for (uint64_t i = 0; i < pages; ++i) {
    if (!pt_.map_4k(virt + i * page, frame.value() + i * page, flags | arch::x86_64::kMapUser)) {
      for (uint64_t j = 0; j < i; ++j) (void)pt_.unmap_4k(virt + j * page);
      free_frames(frame.value(), pages);
      return R::err(VmError::OutOfMemory);
    }
  }
  return R::ok(FrameRun{frame.value(), pages});
}

void AddressSpace::unmap(uint64_t virt) { (void)pt_.unmap_4k(virt); }

bool AddressSpace::protect(uint64_t virt, uint64_t flags) {
  if (virt >= abi::syscall::kUserTop) return false;
  return pt_.protect_4k(virt, flags | arch::x86_64::kMapUser);
}

bool AddressSpace::protect_phys(uint64_t virt, uint64_t phys, uint64_t flags) {
  if (virt >= abi::syscall::kUserTop) return false;
  return pt_.remap_4k(virt, phys, flags | arch::x86_64::kMapUser);
}

void AddressSpace::destroy() {
  pt_.destroy_lower_half(&free_frame, nullptr);
  free_frames(pt_.root());
}

}  // namespace kernel::mm
