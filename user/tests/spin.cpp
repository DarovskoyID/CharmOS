// Вычисления без системных вызовов: ядро вытесняет процесс по таймеру.
#include <charm/sys.hpp>

extern "C" int64_t charm_main() {
  uint64_t deadline = charm::now_ns() + 300'000'000;
  volatile uint64_t work = 0;
  do {
    for (int i = 0; i < 1'000'000; ++i) work = work + 1;
  } while (charm::now_ns() < deadline);
  charm::print("spin: done\n");
  return 7;
}
