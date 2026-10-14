// Первый процесс: версия ABI и вывод в журнал ядра.
#include <charm/sys.hpp>

extern "C" int64_t charm_main() {
  auto v = charm::abi_version();
  if (!v.ok() || (v.value >> 16) != abi::syscall::kVersionMajor) return 1;
  charm::print("hello from ring 3\n");
  return 0;
}
