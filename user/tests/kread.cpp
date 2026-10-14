// Чтение памяти ядра из ring 3: страница есть, но без U/S — #PF, процесс убит.
#include <charm/sys.hpp>

extern "C" int64_t charm_main() {
  charm::print("kread: reading kernel memory\n");
  volatile uint64_t v = *reinterpret_cast<volatile uint64_t*>(0xffffffff80000000ull);
  (void)v;
  charm::print("kread: read kernel memory (BUG)\n");
  return 0;
}
