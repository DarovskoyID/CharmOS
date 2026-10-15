// Завершение многопоточного процесса (docs/abi/process.md §4): главный поток
// выходит через thread_exit (процесс жив), два потока спят без срока — в
// futex и в port_wait, третий вызывает process_exit(5). Процесс обязан
// завершиться с кодом 5: спящие потоки прерываются.
#include "check.hpp"

namespace {

constexpr uint64_t kStack = 16 * 1024;
volatile uint32_t g_never = 0;
volatile uint32_t g_asleep = 0;
charm::Handle g_port = 0;

__attribute__((force_align_arg_pointer)) void sleeper_futex(uint64_t) {
  __atomic_fetch_add(&g_asleep, 1, __ATOMIC_SEQ_CST);
  (void)charm::futex_wait(&g_never, 0, charm::kForever);
  charm::thread_exit(0);
}

__attribute__((force_align_arg_pointer)) void sleeper_port(uint64_t) {
  __atomic_fetch_add(&g_asleep, 1, __ATOMIC_SEQ_CST);
  charm::PortPacket pkt;
  (void)charm::port_wait(g_port, &pkt, charm::kForever);
  charm::thread_exit(0);
}

__attribute__((force_align_arg_pointer)) void killer(uint64_t) {
  while (__atomic_load_n(&g_asleep, __ATOMIC_SEQ_CST) != 2) charm::yield();
  charm::sleep_until(charm::now_ns() + 50'000'000);  // пусть уснут
  charm::print("thread_kill: process_exit from a thread\n");
  charm::process_exit(5);
}

uint64_t stack() {
  auto v = charm::vmo_create(kStack);
  if (!v.ok()) return 0;
  auto m = charm::vmo_map(static_cast<charm::Handle>(v.value), charm::rights::kRead | charm::rights::kWrite);
  return m.ok() ? m.value + kStack : 0;
}

}  // namespace

extern "C" int64_t charm_main() {
  auto port = charm::port_create();
  EXPECT(port.ok());
  g_port = static_cast<charm::Handle>(port.value);
  uint64_t s1 = stack(), s2 = stack(), s3 = stack();
  EXPECT(s1 != 0 && s2 != 0 && s3 != 0);
  EXPECT(charm::thread_create(sleeper_futex, s1, 0).ok());
  EXPECT(charm::thread_create(sleeper_port, s2, 0).ok());
  EXPECT(charm::thread_create(killer, s3, 0).ok());
  charm::thread_exit(77);  // не последний: код процесса задаст process_exit
}
