// Тестовая программа не из магазина (M8.5): своя папка данных и сеть.
#include <stdio.h>

extern "C" int main(int, char**) {
  FILE* f = fopen("/note.txt", "w");  // корень программы — её папка данных
  if (f != nullptr) {
    fputs("saved\n", f);
    fclose(f);
  }
  printf("free app: data folder %s\n", f != nullptr ? "writable" : "missing");
  return 0;
}
