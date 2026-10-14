// Изоляция памяти: два экземпляра на одном CPU пишут разные значения по
// одному и тому же виртуальному адресу; каждый должен видеть своё.
#include <charm/sys.hpp>

namespace {
volatile uint64_t g_marker;
}

extern "C" int64_t charm_main() {
  uint64_t mine = charm::now_ns() | 1;
  g_marker = mine;
  for (int i = 0; i < 300; ++i) {
    charm::yield();
    if (g_marker != mine) {
      charm::print("iso: memory shared between processes (BUG)\n");
      return 1;
    }
  }
  charm::print("iso: private memory ok\n");
  return 0;
}
