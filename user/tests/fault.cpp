// Обращение по нулевому адресу: ядро должно убить процесс, а не упасть.
#include <charm/sys.hpp>

extern "C" int64_t charm_main() {
  charm::print("fault: writing to address 0\n");
  *reinterpret_cast<volatile int*>(0) = 1;
  charm::print("fault: survived (BUG)\n");
  return 0;
}
