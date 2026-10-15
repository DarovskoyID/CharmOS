// ls: содержимое каталогов (каталоги — с «/»), по алфавиту.
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

struct Entry {
  char name[64];
  bool dir;
};

int by_name(const void* a, const void* b) {
  return strcmp(static_cast<const Entry*>(a)->name, static_cast<const Entry*>(b)->name);
}

int list(const char* path) {
  DIR* d = opendir(path);
  if (d == nullptr) {
    printf("ls: %s: %s\n", path, strerror(errno));
    return 1;
  }
  Entry e[128];
  size_t n = 0;
  while (struct dirent* de = readdir(d)) {
    if (n == 128) break;
    snprintf(e[n].name, sizeof e[n].name, "%s", de->d_name);
    e[n++].dir = de->d_type == DT_DIR;
  }
  closedir(d);
  qsort(e, n, sizeof e[0], by_name);
  for (size_t i = 0; i < n; ++i) printf("%s%s\n", e[i].name, e[i].dir ? "/" : "");
  return 0;
}

}  // namespace

extern "C" int main(int argc, char** argv) {
  if (argc < 2) return list(".");
  int rc = 0;
  for (int i = 1; i < argc; ++i) {
    if (argc > 2) printf("%s:\n", argv[i]);
    rc |= list(argv[i]);
  }
  return rc;
}
