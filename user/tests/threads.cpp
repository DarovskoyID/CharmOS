// Потоки (docs/abi/process.md §4): четыре потока со стеками в VMO считают
// общий счётчик под мьютексом на futex, у каждого свой TLS (FS-база) и своя
// плавающая точка; главный ждёт их пакетами ThreadExited через порт.
#include "check.hpp"

using charm::Status;

namespace {

constexpr int kThreads = 4;
constexpr int kRounds = 20000;
constexpr uint64_t kStack = 64 * 1024;

// Мьютекс: 0 — свободен, 1 — занят, 2 — занят и есть ждущие.
volatile uint32_t g_mutex = 0;
uint64_t g_counter = 0;

void lock() {
  uint32_t c = __sync_val_compare_and_swap(&g_mutex, 0u, 1u);
  if (c == 0) return;
  if (c != 2) c = __atomic_exchange_n(&g_mutex, 2u, __ATOMIC_ACQUIRE);
  while (c != 0) {
    (void)charm::futex_wait(&g_mutex, 2, charm::kForever);
    c = __atomic_exchange_n(&g_mutex, 2u, __ATOMIC_ACQUIRE);
  }
}

void unlock() {
  if (__atomic_exchange_n(&g_mutex, 0u, __ATOMIC_RELEASE) == 2) (void)charm::futex_wake(&g_mutex, 1);
}

struct Tls {
  Tls* self;  // %fs:0 — указатель на себя (psABI, TLS variant II)
  uint64_t id;
};
Tls g_tls[kThreads];

uint64_t fs_read(uint64_t off) {
  uint64_t v;
  asm volatile("mov %%fs:(%1), %0" : "=r"(v) : "r"(off));
  return v;
}

__attribute__((force_align_arg_pointer)) void worker(uint64_t id) {
  g_tls[id] = {&g_tls[id], id};
  if (charm::thread_set_fs(reinterpret_cast<uint64_t>(&g_tls[id])) != Status::Ok) charm::thread_exit(100);
  double acc = 0.0;
  for (int i = 1; i <= kRounds; ++i) {
    lock();
    ++g_counter;
    unlock();
    acc += static_cast<double>(i) * static_cast<double>(id + 1);
    if (i % 1000 == 0) charm::yield();
    if (fs_read(8) != id || fs_read(0) != reinterpret_cast<uint64_t>(&g_tls[id])) charm::thread_exit(101);
  }
  double want = static_cast<double>(id + 1) * (static_cast<double>(kRounds) * (kRounds + 1) / 2.0);
  charm::thread_exit(acc == want ? static_cast<int64_t>(10 + id) : 102);
}

}  // namespace

extern "C" int64_t charm_main() {
  // futex: значение не совпало — сразу WouldBlock; совпало — срок.
  volatile uint32_t word = 5;
  EXPECT(charm::futex_wait(&word, 6, charm::kForever) == Status::WouldBlock);
  EXPECT(charm::futex_wait(&word, 5, charm::now_ns() + 20'000'000) == Status::TimedOut);
  EXPECT(charm::futex_wake(&word, 1).value == 0);
  EXPECT(charm::futex_wait(reinterpret_cast<volatile uint32_t*>(2), 0, 0) == Status::InvalidArgs);
  EXPECT(charm::thread_create(worker, 0x100000008ull, 0).status == Status::InvalidArgs);
  EXPECT(charm::thread_set_fs(0xffff800000000000ull) == Status::InvalidArgs);

  auto port = charm::port_create();
  EXPECT(port.ok());
  charm::Handle th[kThreads];
  for (int i = 0; i < kThreads; ++i) {
    auto v = charm::vmo_create(kStack);
    EXPECT(v.ok());
    auto m = charm::vmo_map(static_cast<charm::Handle>(v.value), charm::rights::kRead | charm::rights::kWrite);
    EXPECT(m.ok());
    auto t = charm::thread_create(worker, m.value + kStack, static_cast<uint64_t>(i));
    EXPECT(t.ok());
    th[i] = static_cast<charm::Handle>(t.value);
    EXPECT(charm::object_bind(static_cast<charm::Handle>(port.value), th[i], static_cast<uint64_t>(100 + i)) == Status::Ok);
  }
  // Главный поток тоже считает (своя FPU), пока ждёт.
  double acc = 0.0;
  for (int i = 1; i <= 1000; ++i) acc += 0.5;
  bool seen[kThreads] = {};
  for (int n = 0; n < kThreads; ++n) {
    charm::PortPacket pkt;
    EXPECT(charm::port_wait(static_cast<charm::Handle>(port.value), &pkt, charm::now_ns() + 30'000'000'000ull) ==
           Status::Ok);
    EXPECT(pkt.type == abi::syscall::kPacketThreadExited);
    uint64_t i = pkt.key - 100;
    EXPECT(i < kThreads && !seen[i]);
    seen[i] = true;
    EXPECT(static_cast<int64_t>(pkt.data0) == static_cast<int64_t>(10 + i));
    abi::syscall::ThreadInfo info;
    EXPECT(charm::thread_info(th[i], &info) == Status::Ok);
    EXPECT(info.state == abi::syscall::kThreadExited && info.exit_code == static_cast<int64_t>(10 + i));
  }
  EXPECT(acc == 500.0);
  EXPECT(g_counter == static_cast<uint64_t>(kThreads) * kRounds);
  char buf[64];
  auto s = lib::core::format_to(buf, sizeof buf, "threads: counter {}\n", g_counter);
  charm::debug_write(s.data(), s.size());
  return 0;
}
