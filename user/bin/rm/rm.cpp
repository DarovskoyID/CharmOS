// rm: удалить файлы и пустые каталоги.
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern "C" int main(int argc, char** argv) {
  int rc = 0;
  for (int i = 1; i < argc; ++i) {
    if (unlink(argv[i]) != 0) {
      printf("rm: %s: %s\n", argv[i], strerror(errno));
      rc = 1;
    }
  }
  return rc;
}
