#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void allocation_checks() {
  const size_t counts[] = {0, 1, 3};
  for (size_t alignment = 1; alignment <= 65536; alignment *= 2) {
    for (size_t count : counts) {
      size_t n = count * alignment;
      auto* p = static_cast<unsigned char*>(aligned_alloc(alignment, n));
      CHECK(p != nullptr && reinterpret_cast<uintptr_t>(p) % alignment == 0);
      for (size_t i = 0; i < n; ++i) p[i] = static_cast<unsigned char>(i * 31 + 7);
      auto* q = static_cast<unsigned char*>(realloc(p, n + 70001));
      CHECK(q != nullptr);
      for (size_t i = 0; i < n; ++i) CHECK(q[i] == static_cast<unsigned char>(i * 31 + 7));
      free(q);
      p = static_cast<unsigned char*>(aligned_alloc(alignment, n));
      CHECK(p != nullptr);
      free(p);
    }
  }
  CHECK(aligned_alloc(0, 0) == nullptr && errno == EINVAL);
  CHECK(aligned_alloc(3, 12) == nullptr && errno == EINVAL);
  CHECK(aligned_alloc(64, 65) == nullptr && errno == EINVAL);
  CHECK(aligned_alloc(size_t{1} << 63, size_t{1} << 63) == nullptr && errno == ENOMEM);
  CHECK(aligned_alloc(1, SIZE_MAX) == nullptr && errno == ENOMEM);
  void* sentinel = reinterpret_cast<void*>(uintptr_t{123});
  void* output = sentinel;
  errno = EBUSY;
  CHECK(posix_memalign(&output, 4, 7) == EINVAL && output == sentinel && errno == EBUSY);
  CHECK(posix_memalign(&output, 24, 7) == EINVAL && output == sentinel && errno == EBUSY);
  CHECK(posix_memalign(&output, 64, SIZE_MAX) == ENOMEM && output == sentinel && errno == EBUSY);
  CHECK(posix_memalign(&output, size_t{1} << 63, size_t{1} << 63) == ENOMEM && output == sentinel && errno == EBUSY);
  CHECK(posix_memalign(&output, 4096, 17) == 0 && errno == EBUSY);
  CHECK(reinterpret_cast<uintptr_t>(output) % 4096 == 0);
  memset(output, 0xa5, 17);
  free(output);
  CHECK(posix_memalign(&output, 64, 0) == 0 && output != nullptr && errno == EBUSY);
  free(output);
  printf("threads runtime: aligned allocation, realloc preservation and overflow ok\n");

  const int errors[] = {0, EPERM, ENOENT, ESRCH, EINTR, EIO, E2BIG, ENOEXEC,
    EBADF, ECHILD, EAGAIN, ENOMEM, EACCES, EFAULT, EBUSY, EEXIST, ENOTDIR,
    EISDIR, EINVAL, EMFILE, ENOTTY, ENOSPC, ESPIPE, EPIPE, ERANGE, ENOSYS,
    ENOTEMPTY, ENOTSOCK, EILSEQ, EOVERFLOW, EADDRINUSE, ENETUNREACH,
    ECONNRESET, ETIMEDOUT, ECONNREFUSED};
  for (int e : errors) {
    char text[100];
    errno = EBUSY;
    CHECK(strerror_r(e, text, sizeof text) == 0 && errno == EBUSY);
    CHECK(strcmp(text, strerror(e)) == 0 && strcmp(text, "Unknown error") != 0);
    size_t n = strlen(text);
    char exact[100];
    memset(exact, 0x55, sizeof exact);
    CHECK(strerror_r(e, exact, n + 1) == 0 && exact[n] == 0 && exact[n + 1] == 0x55);
    CHECK(strerror_r(e, exact, n) == ERANGE && exact[n - 1] == 0 && exact[n + 1] == 0x55);
  }
  char short_buffer[] = {'a', 'b', 'c'};
  CHECK(strerror_r(EINVAL, short_buffer, 1) == ERANGE && short_buffer[0] == 0 && short_buffer[1] == 'b');
  CHECK(strerror_r(EINVAL, short_buffer, 0) == ERANGE && short_buffer[1] == 'b');
  CHECK(strerror_r(EINVAL, nullptr, 0) == ERANGE);
  CHECK(strerror_r(EINVAL, nullptr, 1) == EINVAL);
  char unknown[30];
  CHECK(strerror_r(-1, unknown, sizeof unknown) == EINVAL && !strcmp(unknown, "Unknown error"));
  CHECK(strerror_r(ELAST + 1, unknown, sizeof unknown) == EINVAL);
  CHECK(errno == EBUSY);
  printf("threads runtime: XSI error buffers and errno preservation ok\n");
}
