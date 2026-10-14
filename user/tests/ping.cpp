// Замер IPC: N обменов «сообщение — ответ» с pong на другом CPU.
#include "check.hpp"

using namespace charm;

extern "C" int64_t charm_main(Handle ch) {
  constexpr uint64_t kRounds = 2000;
  uint64_t t0 = now_ns();
  uint64_t c0 = __builtin_ia32_rdtsc();
  for (uint64_t i = 0; i < kRounds; ++i) {
    EXPECT(channel_write(ch, &i, sizeof i, nullptr, 0) == Status::Ok);
    uint64_t back = 0;
    Handle hs[1];
    auto r = channel_read(ch, &back, sizeof back, hs, 1, kForever);
    EXPECT(r.ok() && back == i + 1);
  }
  uint64_t cycles = (__builtin_ia32_rdtsc() - c0) / kRounds;
  uint64_t ns = (now_ns() - t0) / kRounds;
  char buf[128];
  auto s = lib::core::format_to(buf, sizeof buf, "ipc: round trip {} ns ({} cycles), {} rounds\n",
                                ns, cycles, kRounds);
  debug_write(s.data(), s.size());
  return 0;
}
