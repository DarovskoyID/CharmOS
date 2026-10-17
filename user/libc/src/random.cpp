#include <sys/random.h>
#include "internal.hpp"

extern "C" ssize_t getrandom(void* buffer, size_t size, unsigned flags) {
  if ((flags & ~GRND_NONBLOCK) || size > static_cast<size_t>(INT64_MAX))
    return libc::fail_errno(EINVAL);
  if (!size) return 0;
  auto base = reinterpret_cast<uint64_t>(buffer);
  if (base >= abi::syscall::kUserTop || size > abi::syscall::kUserTop - base)
    return libc::fail_errno(EFAULT);
  size_t offset = 0;
  while (offset < size) {
    size_t count = size - offset;
    if (count > 256) count = 256;
    auto result = charm::random_read(reinterpret_cast<void*>(base + offset), count, flags);
    if (!result.ok()) return offset ? static_cast<ssize_t>(offset) : libc::fail(result.status);
    offset += count;
  }
  return static_cast<ssize_t>(offset);
}

extern "C" int getentropy(void* buffer, size_t size) {
  if (size > 256) return libc::fail_errno(EIO);
  auto result = charm::random_read(buffer, size);
  return result.ok() ? 0 : libc::fail(result.status);
}
