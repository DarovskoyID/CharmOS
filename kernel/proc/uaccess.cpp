#include "uaccess.hpp"

#include <abi/syscall.hpp>

extern "C" uint64_t uaccess_copy(void* dst, const void* src, uint64_t n);
extern "C" const char uaccess_fault_ip[], uaccess_fixup[];

namespace kernel::proc {

namespace {

bool user_range_ok(uint64_t addr, size_t size) {
  uint64_t end = addr + size;
  return end >= addr && end <= abi::syscall::kUserTop;
}

}  // namespace

bool copy_from_user(void* dst, uint64_t user_src, size_t size) {
  if (!user_range_ok(user_src, size)) return false;
  if (size == 0) return true;
  return uaccess_copy(dst, reinterpret_cast<const void*>(user_src), size) == 0;
}

bool copy_to_user(uint64_t user_dst, const void* src, size_t size) {
  if (!user_range_ok(user_dst, size)) return false;
  if (size == 0) return true;
  return uaccess_copy(reinterpret_cast<void*>(user_dst), src, size) == 0;
}

bool fixup_uaccess_fault(arch::x86_64::InterruptFrame& frame) {
  if ((frame.cs & 3) != 0 || frame.rip != reinterpret_cast<uint64_t>(uaccess_fault_ip)) {
    return false;
  }
  frame.rip = reinterpret_cast<uint64_t>(uaccess_fixup);
  return true;
}

}  // namespace kernel::proc
