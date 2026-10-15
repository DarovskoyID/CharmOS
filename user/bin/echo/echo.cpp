// echo: аргументы через пробел.
#include <stdio.h>

extern "C" int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) printf(i > 1 ? " %s" : "%s", argv[i]);
  printf("\n");
  return 0;
}
