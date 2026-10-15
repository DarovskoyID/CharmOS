// Последний поток (docs/abi/process.md §4): главный выходит через thread_exit,
// процесс завершается с кодом последнего потока (9).
#include "check.hpp"

namespace {

__attribute__((force_align_arg_pointer)) void last(uint64_t) {
  charm::sleep_until(charm::now_ns() + 30'000'000);
  charm::thread_exit(9);
}

}  // namespace

extern "C" int64_t charm_main() {
  auto v = charm::vmo_create(16 * 1024);
  EXPECT(v.ok());
  auto m = charm::vmo_map(static_cast<charm::Handle>(v.value), charm::rights::kRead | charm::rights::kWrite);
  EXPECT(m.ok());
  auto t = charm::thread_create(last, m.value + 16 * 1024, 0);
  EXPECT(t.ok());
  charm::thread_exit(1);
}
