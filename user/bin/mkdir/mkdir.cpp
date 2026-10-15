// mkdir: создать каталоги.
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

extern "C" int main(int argc, char** argv) {
  int rc = 0;
  for (int i = 1; i < argc; ++i) {
    if (mkdir(argv[i], 0755) != 0) {
      printf("mkdir: %s: %s\n", argv[i], strerror(errno));
      rc = 1;
    }
  }
  return rc;
}
