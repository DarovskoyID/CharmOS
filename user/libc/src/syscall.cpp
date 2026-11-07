// Минимальный syscall(2): V8 использует __NR_gettid и __NR_mseal. CharmOS не
// реализует Linux-таблицу системных вызовов, поэтому поддерживаем только то,
// что действительно есть, а остальное честно возвращает ENOSYS.
#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "internal.hpp"

extern "C" long syscall(long number, ...) {
  switch (number) {
    case __NR_gettid:
      return static_cast<long>(gettid());
    case __NR_mseal:
      return libc::fail_errno(ENOSYS);  // no memory sealing
    default:
      return libc::fail_errno(ENOSYS);
  }
}
