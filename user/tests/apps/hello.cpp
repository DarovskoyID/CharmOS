// Тестовая программа магазина (M8.5): пакет подписан ключом магазина.
#include <stdio.h>

extern "C" int main(int argc, char** argv) {
  printf("hello from a store app (%s, %d args)\n", argv[0], argc);
  return 0;
}
