#include <errno.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include "internal.hpp"

extern "C" int ioctl(int fd, unsigned long request, ...) {
  libc::FdLease value(fd);
  if (!value) return libc::fail_errno(EBADF);
  if (request != FIONBIO && request != FIONREAD) return libc::fail_errno(ENOTSUP);
  va_list args;
  va_start(args, request);
  int* argument = va_arg(args, int*);
  va_end(args);
  if (!argument) return libc::fail_errno(EFAULT);
  // Readiness does not supply a byte count. Leave the output untouched.
  if (request == FIONREAD) return libc::fail_errno(ENOTSUP);
  // Update the retained description without a close/reuse race.
  __atomic_store_n(&value->nonblock, *argument != 0, __ATOMIC_RELAXED);
  return 0;
}
