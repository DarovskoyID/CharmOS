// corrupt <файл> <смещение>: инвертировать один байт (для тестов M8.5).
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern "C" int main(int argc, char** argv) {
  if (argc != 3) return 2;
  int fd = open(argv[1], O_RDWR);
  if (fd < 0) {
    printf("corrupt: cannot open %s\n", argv[1]);
    return 1;
  }
  off_t off = atol(argv[2]);
  unsigned char b;
  if (pread(fd, &b, 1, off) != 1) return 1;
  b = static_cast<unsigned char>(b ^ 0xff);
  if (pwrite(fd, &b, 1, off) != 1) return 1;
  close(fd);
  printf("corrupt: flipped byte %ld of %s\n", static_cast<long>(off), argv[1]);
  return 0;
}
