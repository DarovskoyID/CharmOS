// cat: файлы подряд в stdout.
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern "C" int main(int argc, char** argv) {
  int rc = 0;
  for (int i = 1; i < argc; ++i) {
    int fd = open(argv[i], O_RDONLY);
    if (fd < 0) {
      printf("cat: %s: %s\n", argv[i], strerror(errno));
      rc = 1;
      continue;
    }
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) fwrite(buf, 1, static_cast<size_t>(n), stdout);
    close(fd);
  }
  return rc;
}
