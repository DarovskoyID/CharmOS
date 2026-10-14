// Каждый системный вызов v1.0 и его ошибки (docs/abi/syscalls.md §3, §5).
#include <charm/sys.hpp>
#include <lib/core/fmt.hpp>

namespace {

using abi::syscall::Status;

int64_t failed(const char* what) {
  char buf[128];
  auto s = lib::core::format_to(buf, sizeof buf, "sys: FAILED {}\n", what);
  charm::debug_write(s.data(), s.size());
  return 1;
}

}  // namespace

extern "C" int64_t charm_main() {
  // Плохие указатели: ядро возвращает BadAddress, а не падает.
  if (charm::debug_write(reinterpret_cast<const void*>(0xffffffff80000000ull), 4).status !=
      Status::BadAddress) {
    return failed("kernel pointer accepted");
  }
  if (charm::debug_write(reinterpret_cast<const void*>(0x1000), 4).status != Status::BadAddress) {
    return failed("unmapped pointer accepted");
  }
  if (charm::debug_write(reinterpret_cast<const void*>(0x7ffffffffffeull), 16).status !=
      Status::BadAddress) {
    return failed("range crossing user top accepted");
  }
  static char big[5000];
  if (charm::debug_write(big, sizeof big).status != Status::InvalidArgs) {
    return failed("oversized debug_write accepted");
  }
  if (charm::raw_syscall(999).status != Status::NotSupported) return failed("unknown syscall");

  uint64_t t0 = charm::now_ns();
  charm::yield();
  uint64_t t1 = charm::now_ns();
  if (t1 < t0) return failed("clock went backwards");

  uint64_t deadline = charm::now_ns() + 20'000'000;
  charm::sleep_until(deadline);
  if (charm::now_ns() < deadline) return failed("woke before deadline");
  charm::sleep_until(0);  // срок в прошлом — возврат сразу

  auto w = charm::debug_write("sys: all checks passed\n", 23);
  if (!w.ok() || w.value != 23) return failed("debug_write result");
  return 42;
}
