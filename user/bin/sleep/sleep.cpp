// sleep: подождать N секунд.
#include <stdlib.h>
#include <unistd.h>

extern "C" int main(int argc, char** argv) {
  if (argc < 2) return 1;
  sleep(static_cast<unsigned>(atoi(argv[1])));
  return 0;
}
