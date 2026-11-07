#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
// Unbiased base-62 names; exclusive creation arbitrates concurrent callers.
bool random_suffix(char* suffix) {
  constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  unsigned filled = 0;
  while (filled < 6) {
    unsigned char random[16];
    if (getentropy(random, sizeof random) != 0) return false;
    for (unsigned char byte : random) {
      if (byte < 248) suffix[filled++] = alphabet[byte % 62];
      if (filled == 6) break;
    }
  }
  return true;
}
char* suffix_of(char* pattern) {
  if (!pattern) { errno = EINVAL; return nullptr; }
  size_t length = strlen(pattern);
  if (length < 6 || strcmp(pattern + length - 6, "XXXXXX")) {
    errno = EINVAL;
    return nullptr;
  }
  return pattern + length - 6;
}
}

extern "C" int mkostemp(char* pattern, int flags) {
  if (flags & ~(O_APPEND | O_CLOEXEC | O_NONBLOCK)) { errno = EINVAL; return -1; }
  char* suffix = suffix_of(pattern);
  if (!suffix) return -1;
  for (unsigned attempt = 0; attempt < 256; ++attempt) {
    if (!random_suffix(suffix)) break;
    int fd = open(pattern, O_RDWR | O_CREAT | O_EXCL | flags, 0600);
    if (fd >= 0) return fd;
    if (errno != EEXIST) break;
  }
  memcpy(suffix, "XXXXXX", 6);
  return -1;
}
extern "C" int mkstemp(char* pattern) { return mkostemp(pattern, 0); }
// tmpfile: создаём временный файл и сразу снимаем имя — дескриптор продолжает
// работать, а файл исчезает при закрытии/перезагрузке (как в POSIX).
extern "C" FILE* tmpfile(void) {
  const char* dir = getenv("TMPDIR");
  if (!dir || !*dir) dir = "/tmp";
  char pattern[256];
  int length = snprintf(pattern, sizeof pattern, "%s/charmos-XXXXXX", dir);
  int fd = -1;
  if (length > 0 && static_cast<size_t>(length) < sizeof pattern) fd = mkostemp(pattern, O_CLOEXEC);
  if (fd < 0) {
    // Минимальный образ может не содержать /tmp; используем текущий каталог.
    strcpy(pattern, "charmos-XXXXXX");
    fd = mkostemp(pattern, O_CLOEXEC);
  }
  if (fd < 0) return nullptr;
  int saved = errno;
  (void)unlink(pattern);
  FILE* file = fdopen(fd, "w+");
  if (!file) {
    saved = errno;
    (void)close(fd);
    errno = saved;
    return nullptr;
  }
  return file;
}
extern "C" char* mkdtemp(char* pattern) {
  char* suffix = suffix_of(pattern);
  if (!suffix) return nullptr;
  for (unsigned attempt = 0; attempt < 256; ++attempt) {
    if (!random_suffix(suffix)) break;
    if (mkdir(pattern, 0700) == 0) return pattern;
    if (errno != EEXIST) break;
  }
  memcpy(suffix, "XXXXXX", 6);
  return nullptr;
}
