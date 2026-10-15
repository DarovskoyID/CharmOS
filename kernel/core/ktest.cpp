#include "ktest.hpp"

#include <arch/x86_64/cpu.hpp>
#include <arch/x86_64/serial.hpp>

#include "../mm/heap.hpp"
#include "../mm/layout.hpp"
#include "../mm/pmm.hpp"
#include "../mm/vmm.hpp"
#include "../mm/stack.hpp"
#include "../block/block.hpp"
#include <abi/process.hpp>

#include "../acpi/acpi.hpp"
#include "../console/console.hpp"
#include "../drivers/devmgr.hpp"
#include "../fs/cache.hpp"
#include "../fs/vfs.hpp"
#include "../net/ip.hpp"
#include "../net/netif.hpp"
#include "../net/dns.hpp"
#include "../net/socket.hpp"
#include "../net/tcp.hpp"
#include "../net/udp.hpp"
#include "../pci/iommu.hpp"
#include "../pci/pci.hpp"
#include "../platform/platform.hpp"
#include "../ipc/ipc.hpp"
#include "../proc/process.hpp"
#include <lib/initrd/initrd.hpp>
#include "../smp/smp.hpp"
#include "../sync/spinlock.hpp"
#include "../sync/wait.hpp"
#include <lib/core/math.hpp>
#include "log.hpp"

namespace kernel::ktest {

using arch::x86_64::InterruptFrame;
using lib::core::StringView;

namespace {

constinit StringView g_selected;

// Что должен увидеть обработчик исключения в текущем тесте.
struct Expectation {
  bool active = false;
  uint64_t vector = 0;
  bool check_cr2 = false;
  uint64_t cr2 = 0;
  uint64_t error_mask = 0;   // биты кода ошибки, которые проверяем
  uint64_t error_value = 0;  // их ожидаемое значение
};
constinit Expectation g_expect;
constinit bool g_expect_panic = false;
constinit uint64_t g_initrd_phys = 0;
constinit uint64_t g_initrd_size = 0;

void expect(uint64_t vector, uint64_t error_mask = 0, uint64_t error_value = 0) {
  g_expect = {true, vector, false, 0, error_mask, error_value};
}

// Байт с инструкцией ret в .data: исполнение должно упасть на NX.
constinit volatile uint8_t g_data_code[16] = {0xc3};
// Константа в .rodata: запись должна упасть на защите страницы.
const volatile uint64_t g_readonly = 0x1234;

// Коды ошибки #PF (SDM Vol. 3A §4.7): P, W/R, I/D.
constexpr uint64_t kPfPresent = 1 << 0;
constexpr uint64_t kPfWrite = 1 << 1;
constexpr uint64_t kPfFetch = 1 << 4;

[[noreturn]] void after_exception() { fail("expected exception did not happen"); }

// --- тесты исключений ---------------------------------------------------------

void test_pf() {
  // Тождественного отображения больше нет: нижние адреса не отображены.
  expect(arch::x86_64::kVectorPageFault, kPfPresent | kPfWrite, kPfWrite);
  g_expect.check_cr2 = true;
  g_expect.cr2 = 0x1000;
  *reinterpret_cast<volatile uint64_t*>(0x1000) = 1;
  after_exception();
}

void test_pf_rodata_write() {
  expect(arch::x86_64::kVectorPageFault, kPfPresent | kPfWrite, kPfPresent | kPfWrite);
  *const_cast<volatile uint64_t*>(&g_readonly) = 0;
  after_exception();
}

void test_pf_nx() {
  expect(arch::x86_64::kVectorPageFault, kPfFetch, kPfFetch);
  auto fn = reinterpret_cast<void (*)()>(const_cast<uint8_t*>(&g_data_code[0]));
  fn();
  after_exception();
}

void test_gp() {
  expect(arch::x86_64::kVectorGeneralProtection);
  (void)*reinterpret_cast<volatile uint64_t*>(0x8000000000000000ull);  // неканонический
  after_exception();
}

void test_de() {
  expect(arch::x86_64::kVectorDivideError);
  asm volatile("xor %%ecx, %%ecx\n div %%rcx" : : "a"(1), "d"(0) : "rcx");
  after_exception();
}

void test_ud() {
  expect(arch::x86_64::kVectorInvalidOpcode);
  asm volatile("ud2");
  after_exception();
}

void test_df() {
  // Стек указывает в никуда: процессор не может положить кадр #PF и
  // поднимает #DF, который обрабатывается на отдельном стеке IST1.
  expect(arch::x86_64::kVectorDoubleFault);
  asm volatile("mov $0x1000, %%rsp\n int3" : : : "memory");
  after_exception();
}

// int3 — ловушка: после обработчика выполнение продолжается.
constinit volatile uint64_t g_breakpoints = 0;
void breakpoint_handler(InterruptFrame&) { g_breakpoints = g_breakpoints + 1; }

void test_bp_resume() {
  arch::x86_64::set_interrupt_handler(arch::x86_64::kVectorBreakpoint, &breakpoint_handler);
  for (int i = 0; i < 3; ++i) asm volatile("int3");
  arch::x86_64::set_interrupt_handler(arch::x86_64::kVectorBreakpoint, nullptr);
  if (g_breakpoints != 3) fail("breakpoint handler not called 3 times");
  kprint("ktest: 3 breakpoints handled, execution resumed\n");
  pass();
}

// --- память ---------------------------------------------------------------------

struct Rng {
  uint64_t s = 0x2545f4914f6cdd1dull;
  uint64_t next() {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
  }
};

void test_mm() {
  // Физические фреймы: выделить всё, проверить уникальность записью, вернуть.
  uint64_t before = mm::free_frame_count();
  uint64_t taken = 0;
  uint64_t chain = 0;  // односвязный список выделенных фреймов внутри них самих
  for (;;) {
    auto r = mm::alloc_frames();
    if (r.is_err()) break;
    auto* page = mm::phys_to_virt<uint64_t>(r.value());
    page[0] = chain;
    page[1] = r.value() ^ 0xa5a5a5a5a5a5a5a5ull;
    chain = r.value();
    ++taken;
  }
  if (taken != before) fail("pmm: could not allocate every free frame");
  uint64_t walked = 0;
  while (chain != 0) {
    auto* page = mm::phys_to_virt<uint64_t>(chain);
    if (page[1] != (chain ^ 0xa5a5a5a5a5a5a5a5ull)) fail("pmm: frame handed out twice");
    uint64_t next = page[0];
    mm::free_frames(chain);
    chain = next;
    ++walked;
  }
  if (walked != taken || mm::free_frame_count() != before) fail("pmm: frames leaked");
  kprint("ktest: pmm allocated and freed {} frames ({} MiB)\n", taken, taken * 4096 >> 20);

  auto big = mm::alloc_frames(512, 512);  // 2 МиБ, выровнено на 2 МиБ
  if (big.is_err() || big.value() % (2 << 20) != 0) fail("pmm: aligned contiguous alloc");
  mm::free_frames(big.value(), 512);

  // Куча: случайные выделения с проверкой содержимого. В куче уже живут
  // структуры потоков (kmain), поэтому сравниваем с объёмом до стресса.
  size_t heap_before = mm::heap_used_bytes();
  constexpr int kSlots = 512;
  static constinit uint8_t* ptr[kSlots] = {};
  static constinit size_t len[kSlots] = {};
  Rng rng;
  for (int step = 0; step < 50000; ++step) {
    int i = static_cast<int>(rng.next() % kSlots);
    if (ptr[i] == nullptr) {
      size_t n = 1 + rng.next() % ((rng.next() % 16 == 0) ? 32768 : 512);
      auto r = mm::kmalloc(n);
      if (r.is_err()) fail("heap: out of memory in stress test");
      ptr[i] = static_cast<uint8_t*>(r.value());
      len[i] = n;
      for (size_t k = 0; k < n; ++k) ptr[i][k] = static_cast<uint8_t>(i);
    } else {
      for (size_t k = 0; k < len[i]; ++k) {
        if (ptr[i][k] != static_cast<uint8_t>(i)) fail("heap: data overwritten");
      }
      mm::kfree(ptr[i]);
      ptr[i] = nullptr;
    }
  }
  for (int i = 0; i < kSlots; ++i) {
    if (ptr[i] != nullptr) mm::kfree(ptr[i]);
  }
  if (!mm::heap_check() || mm::heap_used_bytes() != heap_before) {
    fail("heap: inconsistent after stress");
  }
  kprint("ktest: heap 50000 ops ok, mapped {} KiB\n", mm::heap_mapped_bytes() >> 10);

  auto huge = mm::kmalloc(size_t{2} << 30);  // больше предела кучи
  if (huge.is_ok()) fail("heap: oversized allocation succeeded");
  pass();
}

// --- платформа --------------------------------------------------------------------

void test_acpi() {
  const auto& madt = platform::madt();
  size_t enabled = 0;
  for (size_t i = 0; i < madt.cpu_count; ++i) enabled += madt.cpus[i].enabled ? 1 : 0;
  if (enabled == 0) fail("MADT: no enabled CPU");
  if (madt.io_apic_count == 0) fail("MADT: no I/O APIC");
  if (platform::io_apic_for(0) == nullptr) fail("no I/O APIC owns GSI 0");
  if (platform::hpet().frequency_hz() < 10'000'000) fail("HPET slower than 10 MHz");
  uint64_t mhz = platform::tsc_hz() / 1'000'000;
  if (mhz < 100 || mhz > 10'000) fail("TSC frequency implausible");
  uint16_t flags = 0;
  uint32_t gsi = lib::acpi::isa_irq_to_gsi(madt, 0, &flags);
  kprint("ktest: {} cpus enabled, ISA IRQ0 -> GSI {} (flags {x})\n", enabled, gsi, flags);
  pass();
}

void test_timer() {
  // Тик планировщика (sched::kTickHz) уже идёт на всех CPU; меряем его на CPU 0.
  constexpr uint64_t kWindowNs = 500'000'000;
  const uint32_t hz = platform::tick_hz();
  const auto& hpet = platform::hpet();
  uint64_t h0 = hpet.counter();
  uint64_t tsc0 = arch::x86_64::rdtsc();
  uint64_t t0 = platform::ticks();
  sched::sleep_ns(kWindowNs);
  uint64_t t1 = platform::ticks();
  uint64_t tsc1 = arch::x86_64::rdtsc();
  uint64_t ns = hpet.ticks_to_ns(hpet.elapsed(h0, hpet.counter()));

  uint64_t ticks = t1 - t0;
  uint64_t measured_hz = ticks * 1'000'000'000 / ns;
  uint64_t tsc_ns = lib::core::mul_div(tsc1 - tsc0, 1'000'000'000, platform::tsc_hz());
  kprint("ktest: {} ticks in {} us -> {} Hz (want {}); TSC says {} us\n", ticks, ns / 1000,
         measured_hz, hz, tsc_ns / 1000);
  if (measured_hz < hz * 9 / 10 || measured_hz > hz * 11 / 10) fail("tick rate off by >10%");
  uint64_t diff = tsc_ns > ns ? tsc_ns - ns : ns - tsc_ns;
  if (diff * 50 > ns) fail("TSC and HPET disagree by >2%");
  pass();
}

constinit volatile uint64_t g_irq_at = 0;
void test_irq_handler(arch::x86_64::InterruptFrame&) {
  g_irq_at = platform::hpet().counter();
  platform::local_apic().eoi();
}

void test_ioapic() {
  const auto& hpet = platform::hpet();
  uint32_t cap = hpet.route_capability(0);
  // Предпочитаем входы ≥ 16: ниже — ISA, их могут переопределять.
  uint32_t gsi = 32;
  for (uint32_t i = 16; i < 32 && gsi == 32; ++i) {
    if ((cap & (1u << i)) != 0 && platform::io_apic_for(i) != nullptr) gsi = i;
  }
  for (uint32_t i = 0; i < 16 && gsi == 32; ++i) {
    if ((cap & (1u << i)) != 0 && platform::io_apic_for(i) != nullptr) gsi = i;
  }
  if (gsi == 32) fail("HPET timer 0 has no usable I/O APIC route");
  kprint("ktest: HPET timer 0 route cap {x}, using GSI {}\n", cap, gsi);

  arch::x86_64::set_interrupt_handler(platform::kVectorTest, &test_irq_handler);
  platform::io_apic_for(gsi)->route(
      gsi, {platform::kVectorTest, false, false,
            static_cast<uint8_t>(platform::local_apic().id())});
  arch::x86_64::sti();

  constexpr uint64_t kDelayNs = 10'000'000;
  uint64_t start = hpet.counter();
  hpet.arm_one_shot(0, gsi, hpet.ns_to_ticks(kDelayNs));
  while (g_irq_at == 0 && hpet.elapsed(start, hpet.counter()) < hpet.ns_to_ticks(10 * kDelayNs)) {
    arch::x86_64::pause();
  }
  arch::x86_64::cli();
  hpet.disarm(0);
  platform::io_apic_for(gsi)->mask(gsi);
  if (g_irq_at == 0) fail("interrupt did not arrive within 100 ms");
  uint64_t after_ns = hpet.ticks_to_ns(hpet.elapsed(start, g_irq_at));
  kprint("ktest: I/O APIC interrupt after {} us\n", after_ns / 1000);
  if (after_ns < kDelayNs * 9 / 10) fail("interrupt arrived too early");
  pass();
}

// --- SMP, блокировки, стеки ---------------------------------------------------------

struct CounterWork {
  sync::Spinlock lock{sync::LockRank::Test, "ktest.counter"};
  volatile uint64_t counter = 0;
  volatile uint64_t per_cpu[smp::kMaxCpus] = {};
};
constinit CounterWork g_counter;
constexpr uint64_t kIncrements = 20'000;  // больше — слишком долго под TCG на CI

void count_under_lock(void*) {
  uint32_t me = smp::this_cpu().index;
  for (uint64_t i = 0; i < kIncrements; ++i) {
    sync::SpinGuard guard(g_counter.lock);
    // Неатомарное чтение-изменение-запись: без блокировки результат был бы меньше.
    g_counter.counter = g_counter.counter + 1;
  }
  g_counter.per_cpu[me] = kIncrements;
}

constinit sync::Semaphore g_done{"ktest.done", 0};

void count_thread(void* arg) {
  count_under_lock(arg);
  g_done.up();
}

void test_smp() {
  uint32_t n = smp::cpu_count();
  size_t expected = 0;
  const auto& madt = platform::madt();
  for (size_t i = 0; i < madt.cpu_count; ++i) expected += madt.cpus[i].enabled ? 1 : 0;
  if (n != expected) fail("not every enabled CPU came online");
  for (uint32_t i = 0; i < n; ++i) {
    if (smp::cpu(i).self != &smp::cpu(i) || !smp::cpu(i).online) fail("bad per-CPU data");
  }
  for (uint32_t i = 0; i < n; ++i) {
    if (sched::spawn("counter", &count_thread, nullptr, static_cast<int>(i)).is_err()) {
      fail("spawn failed");
    }
  }
  for (uint32_t i = 0; i < n; ++i) g_done.down();
  for (uint32_t i = 0; i < n; ++i) {
    if (g_counter.per_cpu[i] != kIncrements) fail("a CPU did not run the work");
  }
  kprint("ktest: {} CPUs x {} locked increments = {}\n", n, kIncrements, g_counter.counter);
  if (g_counter.counter != n * kIncrements) fail("lost updates under spinlock");
  pass();
}

// --- планировщик -------------------------------------------------------------------

constexpr int kMutexThreads = 32;
constexpr int kMutexRounds = 300;
constinit sync::Mutex g_mutex{"ktest.mutex"};
constinit volatile uint64_t g_mutex_counter = 0;
constinit volatile uint32_t g_ran_on[kMutexThreads] = {};

void mutex_thread(void* arg) {
  auto idx = reinterpret_cast<uint64_t>(arg);
  g_ran_on[idx] = smp::this_cpu().index;
  for (int i = 0; i < kMutexRounds; ++i) {
    sync::MutexGuard guard(g_mutex);
    uint64_t v = g_mutex_counter;
    if ((i & 7) == 0) sched::yield();  // уступить CPU внутри критической секции
    g_mutex_counter = v + 1;
  }
  g_done.up();
}

void test_mutex() {
  for (int i = 0; i < kMutexThreads; ++i) {
    auto r = sched::spawn("mutex", &mutex_thread, reinterpret_cast<void*>(uint64_t(i)));
    if (r.is_err()) fail("spawn failed");
  }
  for (int i = 0; i < kMutexThreads; ++i) g_done.down();
  bool used[smp::kMaxCpus] = {};
  uint32_t cpus = 0;
  for (int i = 0; i < kMutexThreads; ++i) {
    if (!used[g_ran_on[i]]) {
      used[g_ran_on[i]] = true;
      ++cpus;
    }
  }
  kprint("ktest: {} threads on {} CPUs, counter {} (want {})\n", kMutexThreads, cpus,
         g_mutex_counter, kMutexThreads * kMutexRounds);
  if (g_mutex_counter != uint64_t{kMutexThreads} * kMutexRounds) fail("mutex lost updates");
  if (smp::cpu_count() > 1 && cpus < 2) fail("threads were not spread across CPUs");
  pass();
}

constinit volatile uint64_t g_spin_count[2] = {};
constinit volatile bool g_spin_stop = false;

void spinner(void* arg) {
  auto idx = reinterpret_cast<uint64_t>(arg);
  // Никогда не уступает CPU сам: прогресс второго потока возможен только
  // через вытеснение по тику.
  while (!g_spin_stop) g_spin_count[idx] = g_spin_count[idx] + 1;
  g_done.up();
}

void test_preempt() {
  int cpu = smp::cpu_count() > 1 ? 1 : 0;
  for (uint64_t i = 0; i < 2; ++i) {
    if (sched::spawn("spinner", &spinner, reinterpret_cast<void*>(i), cpu).is_err()) {
      fail("spawn failed");
    }
  }
  sched::sleep_ns(300'000'000);
  uint64_t a = g_spin_count[0], b = g_spin_count[1];
  g_spin_stop = true;
  g_done.down();
  g_done.down();
  uint64_t switches = sched::context_switches(static_cast<uint32_t>(cpu));
  kprint("ktest: spinners on cpu {}: {} and {} iterations, {} switches\n", cpu, a, b, switches);
  if (a == 0 || b == 0) fail("a spinner never ran: no preemption");
  pass();
}

constexpr int kSleepers = 6;
constinit volatile int g_wake_order[kSleepers] = {};
constinit volatile int g_wake_pos = 0;

void sleeper(void* arg) {
  auto i = static_cast<int>(reinterpret_cast<uint64_t>(arg));
  // Обратный порядок запуска: проснуться должны по длительности сна.
  sched::sleep_ns(uint64_t(kSleepers - i) * 30'000'000);
  int pos = __atomic_fetch_add(&g_wake_pos, 1, __ATOMIC_ACQ_REL);
  g_wake_order[pos] = i;
  g_done.up();
}

void test_sleep() {
  uint64_t t0 = platform::now_ns();
  sched::sleep_ns(50'000'000);
  uint64_t slept = platform::now_ns() - t0;
  kprint("ktest: sleep 50 ms took {} us\n", slept / 1000);
  if (slept < 50'000'000) fail("woke up early");
  // Верхняя граница щедрая: тик 10 мс, плюс задержки эмулятора на CI.
  if (slept > 250'000'000) fail("woke up far too late");

  for (int i = 0; i < kSleepers; ++i) {
    // Все на одном CPU, чтобы порядок определялся только сроками.
    if (sched::spawn("sleeper", &sleeper, reinterpret_cast<void*>(uint64_t(i)), 0).is_err()) {
      fail("spawn failed");
    }
  }
  for (int i = 0; i < kSleepers; ++i) g_done.down();
  for (int i = 0; i < kSleepers; ++i) {
    if (g_wake_order[i] != kSleepers - 1 - i) fail("sleepers woke out of order");
  }
  kprint("ktest: {} sleepers woke in deadline order\n", kSleepers);
  pass();
}

void churn_thread(void*) { g_done.up(); }

void test_churn() {
  constexpr int kBatch = 50;
  constexpr int kRounds = 40;
  size_t heap_after_first = 0;
  uint64_t created0 = sched::threads_created();
  for (int round = 0; round < kRounds; ++round) {
    for (int i = 0; i < kBatch; ++i) {
      if (sched::spawn("churn", &churn_thread, nullptr).is_err()) fail("spawn failed");
    }
    for (int i = 0; i < kBatch; ++i) g_done.down();
    sched::sleep_ns(20'000'000);  // дать завершившимся уйти с CPU в зомби
    if (round == 0) heap_after_first = mm::heap_used_bytes();
  }
  size_t heap_now = mm::heap_used_bytes();
  kprint("ktest: {} threads created and exited, heap {} -> {} bytes\n",
         sched::threads_created() - created0, heap_after_first, heap_now);
  // Структуры и стеки завершённых потоков переиспользуются: память не растёт.
  if (heap_now > heap_after_first + 64 * sizeof(sched::Thread)) fail("thread memory leaks");
  pass();
}

void test_lock_order() {
  static constinit sync::Spinlock outer{sync::LockRank::Heap, "ktest.outer"};
  static constinit sync::Spinlock inner{sync::LockRank::Test, "ktest.inner"};
  g_expect_panic = true;
  sync::SpinGuard a(outer);
  sync::SpinGuard b(inner);  // ранг ниже удерживаемого — паника
  fail("lock order violation not detected");
}

extern "C" void call_on_stack(void (*fn)(void*), void* arg, uint64_t stack_top);

// Граница на 1 млн уровней (~500 МиБ стека) недостижима: защитная страница
// сработает на ~30-м уровне. Она нужна только чтобы рекурсия не была формально
// бесконечной.
[[gnu::noinline]] uint64_t recurse(uint64_t depth) {
  volatile uint8_t pad[512];
  pad[0] = static_cast<uint8_t>(depth);
  if (depth > 1'000'000) return pad[0];
  return recurse(depth + 1) + pad[0];
}

void overflow(void*) { (void)recurse(0); }

void test_guard() {
  auto stack = mm::alloc_kernel_stack(16 * 1024);
  if (stack.is_err()) fail("no stack");
  // Переполнение упирается в защитную страницу: #PF, кадр которого положить
  // уже некуда, — #DF на стеке IST1.
  expect(arch::x86_64::kVectorDoubleFault);
  call_on_stack(&overflow, nullptr, stack.value().top);
  fail("stack overflow not caught");
}

// --- процессы (ring 3) --------------------------------------------------------------

void test_user() {
  if (g_initrd_phys == 0) fail("no initrd");
  lib::initrd::Archive archive;
  auto opened = archive.open(mm::phys_to_virt<const void>(g_initrd_phys), g_initrd_size);
  if (opened.is_err()) fail(lib::initrd::error_name(opened.error()));
  kprint("ktest: initrd has {} files\n", archive.count());

  struct Run {
    const char* file;
    int64_t want;
    int cpu;  // пары на одном CPU — проверка изоляции и сохранения FPU
  };
  const bool many = smp::cpu_count() >= 4;
  const Run runs[] = {
      {"bin/hello", 0, sched::kAnyCpu},
      {"bin/sys", 42, sched::kAnyCpu},
      {"bin/fault", proc::kExitKilled, sched::kAnyCpu},
      {"bin/kread", proc::kExitKilled, sched::kAnyCpu},
      {"bin/fpu", 0, many ? 1 : 0},
      {"bin/fpu", 0, many ? 1 : 0},
      {"bin/iso", 0, many ? 2 : 0},
      {"bin/iso", 0, many ? 2 : 0},
      {"bin/spin", 7, many ? 3 : 0},
  };
  constexpr size_t kRuns = sizeof runs / sizeof runs[0];

  // Учёт памяти: свободные фреймы + страницы стеков ядра + страницы кучи.
  // Стеки и куча — пулы ядра; они растут, если завершившийся поток ещё не ушёл
  // в зомби (зависит от расклада по времени). Память процессов же обязана
  // вернуться полностью, поэтому сумма после двух прогонов совпадает точно.
  auto accounted = [] {
    return mm::free_frame_count() + mm::kernel_stack_pages() + mm::heap_mapped_bytes() / 4096;
  };
  uint64_t frames_after_first = 0;
  for (int round = 0; round < 2; ++round) {
    proc::Process* procs[kRuns] = {};
    for (size_t i = 0; i < kRuns; ++i) {
      lib::initrd::File f;
      if (!archive.find(runs[i].file, &f)) fail("program missing from initrd");
      auto p = proc::spawn_process(f.name.substr(4), f.data, f.size, runs[i].cpu);
      if (p.is_err()) fail("spawn_process failed");
      procs[i] = p.value();
    }
    bool ok = true;
    for (size_t i = 0; i < kRuns; ++i) {
      int64_t code = proc::wait_process(procs[i]);
      if (round == 0) kprint("ktest: {} exited with {} (want {})\n", runs[i].file, code, runs[i].want);
      ok = ok && code == runs[i].want;
    }
    if (!ok) fail("unexpected exit code");
    sched::sleep_ns(50'000'000);  // завершившиеся потоки уходят в зомби
    if (round == 0) frames_after_first = accounted();
  }
  uint64_t frames_after_second = accounted();
  kprint("ktest: accounted frames after run 1: {}, after run 2: {}\n", frames_after_first,
         frames_after_second);
  if (frames_after_second != frames_after_first) fail("process memory leaked");
  pass();
}

// --- капабилити и IPC (M4) ------------------------------------------------------------

void test_ipc() {
  if (g_initrd_phys == 0) fail("no initrd");
  lib::initrd::Archive archive;
  if (archive.open(mm::phys_to_virt<const void>(g_initrd_phys), g_initrd_size).is_err()) {
    fail("bad initrd");
  }
  auto pair = [](ipc::Cap** a, ipc::Cap** b) {
    if (ipc::create_channel(a, b) != ipc::Status::Ok) fail("create_channel");
  };
  ipc::Cap *ab0, *ab1, *ac0, *ac1, *pp0, *pp1;
  pair(&ab0, &ab1);
  pair(&ac0, &ac1);
  pair(&pp0, &pp1);
  // Конец A↔C попадает к A первым сообщением в его стартовом канале.
  if (ipc::kernel_send(ab1, "ac", 2, &ac0, 1) != ipc::Status::Ok) fail("kernel_send");

  struct Run {
    const char* file;
    ipc::Cap* bootstrap;
    int cpu;
    int64_t want;
  };
  const bool many = smp::cpu_count() >= 3;
  const Run runs[] = {
      {"bin/cap_a", ab0, sched::kAnyCpu, 0},
      {"bin/cap_b", ab1, sched::kAnyCpu, proc::kExitKilled},  // тронул отозванное
      {"bin/cap_c", ac1, sched::kAnyCpu, 0},
      {"bin/chan", nullptr, sched::kAnyCpu, 0},
      {"bin/ping", pp0, many ? 1 : 0, 0},
      {"bin/pong", pp1, many ? 2 : 0, 0},
      {"bin/port", nullptr, sched::kAnyCpu, 0},
  };
  constexpr size_t kRuns = sizeof runs / sizeof runs[0];
  proc::Process* procs[kRuns] = {};
  for (size_t i = 0; i < kRuns; ++i) {
    lib::initrd::File f;
    if (!archive.find(runs[i].file, &f)) fail("program missing from initrd");
    auto p = proc::spawn_process(f.name.substr(4), f.data, f.size, runs[i].cpu, runs[i].bootstrap);
    if (p.is_err()) fail("spawn_process failed");
    procs[i] = p.value();
  }
  bool ok = true;
  for (size_t i = 0; i < kRuns; ++i) {
    int64_t code = proc::wait_process(procs[i]);
    kprint("ktest: {} exited with {} (want {})\n", runs[i].file, code, runs[i].want);
    ok = ok && code == runs[i].want;
  }
  if (!ok) fail("unexpected exit code");
  pass();
}

// --- PCI (M5) ---------------------------------------------------------------------------

// Учебное устройство QEMU edu (docs/specs/qemu-edu.rst).
constexpr uint16_t kEduVendor = 0x1234;
constexpr uint16_t kEduDevice = 0x11e8;

const pci::Function* find_edu() {
  for (uint32_t i = 0; i < pci::function_count(); ++i) {
    const pci::Function& f = pci::function(i);
    if (f.vendor == kEduVendor && f.device_id == kEduDevice) return &f;
  }
  return nullptr;
}

void test_pci() {
  const pci::Function* f = find_edu();
  if (f == nullptr) fail("edu device not found");
  const pci::Bar& bar = f->bars[0];
  kprint("ktest: edu at {}:{}.{}, BAR0 kind {} base {x} size {x}, msi cap {x}\n", f->bus,
         f->device, f->function, static_cast<uint32_t>(bar.kind), bar.base, bar.size, f->msi_cap);
  if (bar.kind != pci::BarKind::Mem32 && bar.kind != pci::BarKind::Mem64) fail("BAR0 is not memory");
  if (bar.size != (1u << 20)) fail("BAR0 size is not 1 MiB");
  if (f->msi_cap == 0) fail("no MSI capability");
  if (f->msix_cap != 0) fail("edu unexpectedly has MSI-X");

  auto mmio = mm::map_mmio(bar.base, bar.size);
  if (mmio.is_err()) fail("cannot map BAR0");
  uint64_t regs = mmio.value();
  pci::write16(*f, pci::kRegCommand,
               static_cast<uint16_t>(pci::read16(*f, pci::kRegCommand) | pci::kCommandMemory));
  auto rd = [&](uint64_t off) { return *reinterpret_cast<volatile uint32_t*>(regs + off); };
  auto wr = [&](uint64_t off, uint32_t v) { *reinterpret_cast<volatile uint32_t*>(regs + off) = v; };

  uint32_t id = rd(0x00);
  kprint("ktest: edu id {x}\n", id);
  if ((id & 0xff) != 0xed) fail("edu identification mismatch");  // 0xRRrr00ed
  wr(0x04, 0xdeadbeefu);
  if (rd(0x04) != ~0xdeadbeefu) fail("liveness register did not invert");
  wr(0x08, 10);
  uint64_t start = platform::now_ns();
  while ((rd(0x20) & 1) != 0) {
    if (platform::now_ns() - start > 1'000'000'000) fail("factorial timed out");
  }
  uint32_t fact = rd(0x08);
  kprint("ktest: edu 10! = {}\n", fact);
  if (fact != 3628800) fail("factorial wrong");
  pass();
}

void test_drivers() {
  // Ядро само нашло edu, запустило драйвер по манифесту; драйвер после
  // проверок падает — ядро перезапускает его 5 раз и сдаётся.
  uint64_t deadline = platform::now_ns() + 30'000'000'000ull;
  devmgr::DriverStats s;
  do {
    sched::sleep_ns(50'000'000);
    s = devmgr::stats(kEduVendor, kEduDevice);
  } while (!s.finished && platform::now_ns() < deadline);
  kprint("ktest: edu driver bound={} runs={} killed={} finished={}\n", s.bound, s.runs, s.killed,
         s.finished);
  if (!s.bound) fail("no driver bound to edu");
  if (!s.finished) fail("supervisor did not finish");
  if (s.runs != devmgr::kMaxRestarts + 1 || s.killed != s.runs) fail("wrong restart count");
  // С IOMMU каждый запуск кончается ошибкой трансляции его же DMA.
  if (pci::iommu::active()) {
    uint64_t faults = pci::iommu::fault_count();
    kprint("ktest: iommu faults={}\n", faults);
    if (faults < s.runs) fail("fewer IOMMU faults than driver runs");
  }
  pass();
}

void test_blk() {
  // Драйвер virtio_blk поднят менеджером; диск — пустой образ (tools/qemu/blk.sh).
  if (!block::wait_online(0, platform::now_ns() + 20'000'000'000ull)) fail("disk0 not online");
  block::DiskInfo info;
  (void)block::disk_info(0, &info);
  kprint("ktest: disk0 {} blocks of {}\n", info.block_count, info.block_size);
  uint64_t pages[4];
  for (auto& p : pages) {
    auto f = mm::alloc_frames();
    if (f.is_err()) fail("no memory");
    p = f.value();
  }
  auto* w0 = mm::phys_to_virt<uint32_t>(pages[0]);
  auto* w1 = mm::phys_to_virt<uint32_t>(pages[1]);
  for (uint32_t i = 0; i < 1024; ++i) {
    w0[i] = 0xc0de0000u + i;
    w1[i] = ~(0xc0de0000u + i);
  }
  if (block::transfer(0, true, 2048, pages, 2) != block::Status::Ok) fail("write failed");
  if (block::flush(0) != block::Status::Ok) fail("flush failed");
  __builtin_memset(mm::phys_to_virt<void>(pages[2]), 0xaa, 4096);
  __builtin_memset(mm::phys_to_virt<void>(pages[3]), 0xaa, 4096);
  if (block::transfer(0, false, 2048, pages + 2, 2) != block::Status::Ok) fail("read failed");
  if (__builtin_memcmp(mm::phys_to_virt<void>(pages[0]), mm::phys_to_virt<void>(pages[2]), 4096) != 0 ||
      __builtin_memcmp(mm::phys_to_virt<void>(pages[1]), mm::phys_to_virt<void>(pages[3]), 4096) != 0) {
    fail("read back differs");
  }
  kprint("ktest: disk0 write, flush, read back ok\n");

  // Драйвер убит прямо перед запросами: они ждут нового запуска и
  // выполняются в нём (docs/abi/block.md §6).
  if (!devmgr::kill_driver(0x1af4, 0x1042)) fail("virtio_blk not running");
  for (uint32_t i = 0; i < 1024; ++i) w0[i] = 0x5eed0000u + i;
  if (block::transfer(0, true, 4096, pages, 1) != block::Status::Ok) fail("write after kill");
  __builtin_memset(mm::phys_to_virt<void>(pages[2]), 0, 4096);
  if (block::transfer(0, false, 4096, pages + 2, 1) != block::Status::Ok) fail("read after kill");
  if (__builtin_memcmp(mm::phys_to_virt<void>(pages[0]), mm::phys_to_virt<void>(pages[2]), 4096) != 0) {
    fail("data after restart differs");
  }
  // Данные, записанные до падения, на месте.
  if (block::transfer(0, false, 2048, pages + 2, 2) != block::Status::Ok) fail("reread");
  if (__builtin_memcmp(mm::phys_to_virt<void>(pages[1]), mm::phys_to_virt<void>(pages[3]), 4096) != 0) {
    fail("old data lost");
  }
  devmgr::DriverStats st = devmgr::stats(0x1af4, 0x1042);
  kprint("ktest: virtio_blk runs={} killed={}\n", st.runs, st.killed);
  if (st.runs != 2 || st.killed != 1) fail("driver was not restarted exactly once");
  // За концом диска — отказ без обращения к драйверу.
  if (block::transfer(0, false, info.block_count - 1, pages, 1) != block::Status::OutOfRange) {
    fail("out of range not rejected");
  }
  for (uint64_t p : pages) mm::free_frames(p);
  pass();
}

// Программа из initrd с корнем ФС в стартовом канале (docs/abi/fs.md §2).
void run_with_root(const char* file) {
  if (!fs::mounted()) fail("root not mounted");
  lib::initrd::Archive archive;
  if (archive.open(mm::phys_to_virt<const void>(g_initrd_phys), g_initrd_size).is_err()) fail("bad initrd");
  lib::initrd::File f;
  if (!archive.find(StringView(file), &f)) fail("program missing from initrd");
  ipc::Cap *mine, *theirs, *root;
  if (ipc::create_channel(&mine, &theirs) != ipc::Status::Ok) fail("create_channel");
  if (fs::root_cap(&root) != ipc::Status::Ok) fail("root_cap");
  if (ipc::kernel_send(mine, "root", 4, &root, 1) != ipc::Status::Ok) fail("kernel_send");
  auto p = proc::spawn_process(f.name.substr(4), f.data, f.size, sched::kAnyCpu, theirs);
  if (p.is_err()) fail("spawn_process failed");
  int64_t code = proc::wait_process(p.value());
  ipc::close_detached(mine);
  kprint("ktest: {} exited with {}\n", file, code);
  if (code != 0) fail("program failed");
}

void test_fs() {
  run_with_root("bin/fs_test");
  fs::CacheStats cs = fs::cache_stats();
  kprint("ktest: cache hits={} misses={} writebacks={}\n", cs.hits, cs.misses, cs.writebacks);
  if (cs.writebacks == 0) fail("nothing written back");
  pass();
}

// Драйвер диска убивается, пока fs_test работает с файлами: запросы
// повторяются в новом запуске, программа завершается успешно, ФС цела
// (её проверяет e2fsck в следующем сценарии).
void killer(void*) {
  sched::sleep_ns(30'000'000);
  bool killed = devmgr::kill_driver(0x1b36, 0x0010);  // NVMe QEMU
  kprint("ktest: nvme driver killed during I/O: {}\n", killed);
}

void test_fs_crash() {
  if (sched::spawn("killer", &killer, nullptr).is_err()) fail("spawn killer");
  run_with_root("bin/fs_test");
  devmgr::DriverStats st = devmgr::stats(0x1b36, 0x0010);
  kprint("ktest: nvme runs={} killed={}\n", st.runs, st.killed);
  if (st.runs < 2 || st.killed < 1) fail("driver was not restarted");
  if (fs::sync_all() != ipc::Status::Ok) fail("sync after restart");
  pass();
}

void test_net() {
  // QEMU user-net (slirp): адрес по DHCP (ожидается 10.0.2.15, шлюз 10.0.2.2).
  uint64_t deadline = platform::now_ns() + 20'000'000'000ull;
  while (!net::link_up()) {
    if (platform::now_ns() > deadline) fail("no link");
    sched::sleep_ns(10'000'000);
  }
  if (net::dhcp_configure(platform::now_ns() + 10'000'000'000ull) != net::Status::Ok) fail("dhcp");
  if (net::config().gateway != net::ipv4(10, 0, 2, 2)) fail("unexpected gateway");
  for (uint16_t seq = 1; seq <= 3; ++seq) {
    uint64_t rtt = 0;
    auto s = net::ping(net::ipv4(10, 0, 2, 2), seq, platform::now_ns() + 2'000'000'000ull, &rtt);
    if (s != net::Status::Ok) fail("ping 10.0.2.2 failed");
    kprint("ktest: ping 10.0.2.2 seq={} ok\n", seq);
  }
  pass();
}

// Эхо по TCP с эхо-сервером на хосте (tools/qemu/tcp.sh, порт 7777; гость
// видит хост как 10.0.2.2). Несколько соединений, затем с потерями.
bool tcp_round(uint32_t conn_no, uint32_t bytes) {
  int c;
  uint64_t deadline = platform::now_ns() + 60'000'000'000ull;
  if (auto cs = net::tcp_connect(net::ipv4(10, 0, 2, 2), 7777, deadline, &c); cs != net::Status::Ok) {
    kprint("ktest: tcp connect {} failed: {}\n", conn_no, static_cast<uint64_t>(cs));
    return false;
  }
  static uint8_t out[8192], in[8192];
  uint32_t sent_total = 0, got_total = 0;
  bool ok = true;
  while (got_total < bytes && ok) {
    if (sent_total < bytes) {
      uint32_t n = bytes - sent_total < sizeof out ? bytes - sent_total : sizeof out;
      for (uint32_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>((sent_total + i) * 7 + conn_no);
      uint32_t s = 0;
      if (auto ss = net::tcp_send(c, out, n, platform::now_ns() + 50'000'000, &s);
          ss != net::Status::Ok && ss != net::Status::TimedOut) {
        kprint("ktest: tcp send status {}\n", static_cast<uint64_t>(ss));
        ok = false;
      }
      sent_total += s;
    }
    uint32_t g = 0;
    net::Status rs = net::tcp_recv(c, in, sizeof in, platform::now_ns() + 50'000'000, &g);
    if (rs != net::Status::Ok && rs != net::Status::TimedOut) {
      kprint("ktest: tcp recv status {}\n", static_cast<uint64_t>(rs));
      ok = false;
    }
    if (rs == net::Status::Ok && g == 0) ok = false;  // сервер закрыл раньше времени
    for (uint32_t i = 0; i < g && ok; ++i) {
      if (in[i] != static_cast<uint8_t>((got_total + i) * 7 + conn_no)) {
        kprint("ktest: tcp round {} wrong byte at {}\n", conn_no, got_total + i);
        ok = false;
      }
    }
    got_total += g;
    if (platform::now_ns() > deadline) {
      kprint("ktest: tcp round {} deadline\n", conn_no);
      ok = false;
    }
  }
  if (!ok) {
    kprint("ktest: tcp round {} failed: sent={} got={}\n", conn_no, sent_total, got_total);
    net::tcp_dump(c);
  }
  (void)net::tcp_shutdown(c, false, true);
  net::tcp_release(c);
  return ok && got_total == bytes;
}

void test_tcp() {
  uint64_t deadline = platform::now_ns() + 20'000'000'000ull;
  while (!net::link_up()) {
    if (platform::now_ns() > deadline) fail("no link");
    sched::sleep_ns(10'000'000);
  }
  if (net::dhcp_configure(platform::now_ns() + 10'000'000'000ull) != net::Status::Ok) fail("dhcp");
  for (uint32_t i = 0; i < 8; ++i) {
    if (!tcp_round(i, 64 * 1024)) fail("echo round failed");
  }
  kprint("ktest: tcp echo 8 connections x 64 KiB ok\n");
  net::tcp_set_drop(10);  // каждый 10-й сегмент TCP (в обе стороны) теряется
  for (uint32_t i = 0; i < 4; ++i) {
    if (!tcp_round(100 + i, 64 * 1024)) fail("echo with loss failed");
  }
  net::tcp_set_drop(0);
  kprint("ktest: tcp echo with 1/10 segment loss ok\n");
  pass();
}

// Эхо-сервер в ring 3 (bin/tcp_echo) на сокетах v1.4; клиент — на хосте
// (tools/qemu/sock.sh, через hostfwd 127.0.0.1:7778 → гость:7777), 10
// соединений по 64 КиБ, каждый 10-й сегмент TCP теряется.
void test_sock() {
  uint64_t deadline = platform::now_ns() + 20'000'000'000ull;
  while (!net::link_up()) {
    if (platform::now_ns() > deadline) fail("no link");
    sched::sleep_ns(10'000'000);
  }
  if (net::dhcp_configure(platform::now_ns() + 10'000'000'000ull) != net::Status::Ok) fail("dhcp");
  net::tcp_set_drop(10);
  lib::initrd::Archive archive;
  if (archive.open(mm::phys_to_virt<const void>(g_initrd_phys), g_initrd_size).is_err()) fail("bad initrd");
  lib::initrd::File f;
  if (!archive.find(StringView("bin/tcp_echo"), &f)) fail("tcp_echo missing");
  ipc::Cap *mine, *theirs, *netc;
  if (ipc::create_channel(&mine, &theirs) != ipc::Status::Ok) fail("create_channel");
  if (net::net_cap(&netc) != ipc::Status::Ok) fail("net_cap");
  if (ipc::kernel_send(mine, "net", 3, &netc, 1) != ipc::Status::Ok) fail("kernel_send");
  auto p = proc::spawn_process(f.name.substr(4), f.data, f.size, sched::kAnyCpu, theirs);
  if (p.is_err()) fail("spawn");
  int64_t code = proc::wait_process(p.value());
  ipc::close_detached(mine);
  net::tcp_set_drop(0);
  kprint("ktest: tcp_echo exited with {}\n", code);
  if (code != 0) fail("tcp_echo failed");
  pass();
}

// UDP-сокеты и DNS из ring 3 (bin/udp_test); на хосте — tools/qemu/udp.sh.
void test_udp() {
  uint64_t deadline = platform::now_ns() + 20'000'000'000ull;
  while (!net::link_up()) {
    if (platform::now_ns() > deadline) fail("no link");
    sched::sleep_ns(10'000'000);
  }
  if (net::dhcp_configure(platform::now_ns() + 10'000'000'000ull) != net::Status::Ok) fail("dhcp");
  net::dns_set_server(net::ipv4(10, 0, 2, 2), 5353);  // заглушка на хосте (порт 53 — только root)
  lib::initrd::Archive archive;
  if (archive.open(mm::phys_to_virt<const void>(g_initrd_phys), g_initrd_size).is_err()) fail("bad initrd");
  lib::initrd::File f;
  if (!archive.find(StringView("bin/udp_test"), &f)) fail("udp_test missing");
  ipc::Cap *mine, *theirs, *netc;
  if (ipc::create_channel(&mine, &theirs) != ipc::Status::Ok) fail("create_channel");
  if (net::net_cap(&netc) != ipc::Status::Ok) fail("net_cap");
  if (ipc::kernel_send(mine, "net", 3, &netc, 1) != ipc::Status::Ok) fail("kernel_send");
  auto p = proc::spawn_process(f.name.substr(4), f.data, f.size, sched::kAnyCpu, theirs);
  if (p.is_err()) fail("spawn");
  int64_t code = proc::wait_process(p.value());
  ipc::close_detached(mine);
  kprint("ktest: udp_test exited with {}\n", code);
  if (code != 0) fail("udp_test failed");
  pass();
}

void test_fs_check() {
  run_with_root("bin/fs_check");
  pass();
}

// Ввод с COM1 (tests/qemu/m8/console.input): строка, затем Ctrl-C.
constinit volatile uint32_t g_console_signal = 0;
void test_console() {
  console::set_control_handler([](uint32_t signo) { g_console_signal = signo; });
  char line[128];
  uint32_t len = 0;
  uint64_t deadline = platform::now_ns() + 60'000'000'000ull;
  while (len < sizeof line - 1) {
    uint32_t got = 0;
    if (console::read(line + len, 1, deadline, &got) != console::Status::Ok) fail("no console input");
    if (line[len] == '\n' || line[len] == '\r') break;
    len += got;
  }
  kprint("ktest: console got: {}\n", StringView(line, len));
  while (g_console_signal == 0) {
    if (platform::now_ns() > deadline) fail("no ctrl-c");
    sched::sleep_ns(10'000'000);
  }
  kprint("ktest: console control signal {}\n", static_cast<uint32_t>(g_console_signal));
  if (g_console_signal != 1) fail("expected Interrupt");
  pass();
}

// Потоки (M8, process.md §4): дважды, с учётом памяти, как test_user.
void test_threads() {
  lib::initrd::Archive archive;
  if (archive.open(mm::phys_to_virt<const void>(g_initrd_phys), g_initrd_size).is_err()) fail("bad initrd");
  struct Run {
    const char* file;
    int64_t want;
  };
  const Run runs[] = {{"bin/threads", 0}, {"bin/thread_kill", 5}, {"bin/thread_last", 9}};
  auto accounted = [] {
    return mm::free_frame_count() + mm::kernel_stack_pages() + mm::heap_mapped_bytes() / 4096;
  };
  uint64_t first = 0;
  for (int round = 0; round < 2; ++round) {
    for (const Run& r : runs) {
      lib::initrd::File f;
      if (!archive.find(r.file, &f)) fail("program missing from initrd");
      auto p = proc::spawn_process(f.name.substr(4), f.data, f.size);
      if (p.is_err()) fail("spawn_process failed");
      int64_t code = proc::wait_process(p.value());
      if (round == 0) kprint("ktest: {} exited with {} (want {})\n", r.file, code, r.want);
      if (code != r.want) fail("unexpected exit code");
    }
    sched::sleep_ns(50'000'000);  // завершившиеся потоки уходят в зомби
    if (round == 0) first = accounted();
  }
  uint64_t second = accounted();
  kprint("ktest: threads accounted frames {} / {}\n", first, second);
  if (first != second) fail("thread memory leaked");
  pass();
}

// Запуск процессов (M8, process.md §2–§5): ядро даёт spawn_parent
// стартовое сообщение "strt" с ролями Root и Job; тот запускает ребёнка с диска.
// args/env — строки с \0 подряд (process.md §2).
void run_with_start(const char* file, const char* args, uint32_t args_bytes, uint32_t argc, const char* env = "",
                    uint32_t env_bytes = 0, uint32_t envc = 0) {
  namespace ap = abi::process;
  if (!fs::mounted()) fail("root not mounted");
  lib::initrd::Archive archive;
  if (archive.open(mm::phys_to_virt<const void>(g_initrd_phys), g_initrd_size).is_err()) fail("bad initrd");
  lib::initrd::File f;
  if (!archive.find(StringView(file), &f)) fail("program missing from initrd");
  ipc::Cap *mine, *theirs, *caps[2];
  if (ipc::create_channel(&mine, &theirs) != ipc::Status::Ok) fail("create_channel");
  if (fs::root_cap(&caps[0]) != ipc::Status::Ok) fail("root_cap");
  if (ipc::create_job_cap(ipc::root_job(), &caps[1]) != ipc::Status::Ok) fail("job cap");
  struct {
    ap::StartHeader h;
    char data[512];
  } msg{};
  if (args_bytes + env_bytes > sizeof msg.data) fail("start message too big");
  __builtin_memcpy(msg.h.magic, ap::kStartMagic, 4);
  msg.h.argc = argc;
  msg.h.args_bytes = args_bytes;
  msg.h.envc = envc;
  msg.h.env_bytes = env_bytes;
  msg.h.handle_count = 2;
  msg.h.roles[0] = ap::kRoleRoot;
  msg.h.roles[1] = ap::kRoleJob;
  __builtin_memcpy(msg.data, args, args_bytes);
  __builtin_memcpy(msg.data + args_bytes, env, env_bytes);
  if (ipc::kernel_send(mine, &msg, sizeof msg.h + args_bytes + env_bytes, caps, 2) != ipc::Status::Ok) fail("kernel_send");
  auto p = proc::spawn_process(f.name.substr(4), f.data, f.size, sched::kAnyCpu, theirs);
  if (p.is_err()) fail("spawn_process failed");
  int64_t code = proc::wait_process(p.value());
  ipc::close_detached(mine);
  kprint("ktest: {} exited with {}\n", file, code);
  if (code != 0) fail("program failed");
}

void run_with_start(const char* file, const char* argv0) {
  run_with_start(file, argv0, static_cast<uint32_t>(StringView(argv0).size()) + 1, 1);
}

void test_libc() {
  static const char kArgs[] = "libc_test\0arg1";
  static const char kEnv[] = "HOME=/home\0LANG=C";
  run_with_start("bin/libc_test", kArgs, sizeof kArgs, 2, kEnv, sizeof kEnv, 2);
  pass();
}

void test_spawn() {
  run_with_start("bin/spawn_parent", "spawn_parent");
  pass();
}

void test_signals() {
  run_with_start("bin/signals", "signals");
  pass();
}

// Пространство имён из DSDT: устройства, методы, _S5, _PRT корня PCI.
void aml_check() {
  auto& in = acpi::interpreter();
  kprint("ktest: aml {} nodes {} methods\n", in.node_count(), in.method_count());
  lib::acpi::aml::Value v{};
  if (in.evaluate("\\_S5", &v) != lib::acpi::aml::Error::Ok || v.type != lib::acpi::aml::Type::Package) fail("_S5");
  kprint("ktest: _S5 SLP_TYPa {}\n", v.items[0].integer);
  if (in.evaluate("\\_SB.PCI0._PRT", &v) != lib::acpi::aml::Error::Ok || v.type != lib::acpi::aml::Type::Package) {
    fail("_PRT");
  }
  kprint("ktest: _PRT {} entries\n", v.size);
}

// M9: выключение через ACPI S5 (_S5 + PM1a_CNT). QEMU выходит с кодом 0.
void test_poweroff() {
  if (!acpi::ready()) fail("AML not loaded");
  aml_check();
  acpi::power_off();
  fail("still running after S5");
}

// M9a: закрытая область (sysarea-crypt.md) смонтирована и читается: файл,
// положенный mkdisk открытым, после шифрования на месте — тот же.
void test_sysarea() {
  if (!fs::system_mounted()) fail("system area not mounted");
  static char buf[128];
  uint64_t got = 0;
  if (fs::system_read("secret.txt", buf, sizeof buf - 1, &got) != ipc::Status::Ok) fail("cannot read secret.txt");
  buf[got] = 0;
  if (!StringView(buf, got).starts_with("sysarea secret: only through the TPM")) fail("wrong content");
  kprint("ktest: system area file ok\n");
  pass();
}

// Без TPM закрытая область не монтируется (журнал решений 2026-10-15).
void test_sysarea_locked() {
  if (fs::system_mounted()) fail("system area mounted without TPM");
  kprint("ktest: system area locked\n");
  pass();
}

struct Test {
  StringView name;
  void (*fn)();
};

constexpr Test kTests[] = {
    {"pf", &test_pf},
    {"pf-rodata", &test_pf_rodata_write},
    {"pf-nx", &test_pf_nx},
    {"gp", &test_gp},
    {"de", &test_de},
    {"ud", &test_ud},
    {"df", &test_df},
    {"bp", &test_bp_resume},
    {"mm", &test_mm},
    {"acpi", &test_acpi},
    {"timer", &test_timer},
    {"ioapic", &test_ioapic},
    {"smp", &test_smp},
    {"lockorder", &test_lock_order},
    {"guard", &test_guard},
    {"mutex", &test_mutex},
    {"preempt", &test_preempt},
    {"sleep", &test_sleep},
    {"churn", &test_churn},
    {"user", &test_user},
    {"ipc", &test_ipc},
    {"pci", &test_pci},
    {"drivers", &test_drivers},
    {"blk", &test_blk},
    {"console", &test_console},
    {"poweroff", &test_poweroff},
    {"sysarea", &test_sysarea},
    {"sysarea_locked", &test_sysarea_locked},
    {"threads", &test_threads},
    {"spawn", &test_spawn},
    {"signals", &test_signals},
    {"libc", &test_libc},
    {"fs", &test_fs},
    {"fs_check", &test_fs_check},
    {"fs_crash", &test_fs_crash},
    {"net", &test_net},
    {"tcp", &test_tcp},
    {"sock", &test_sock},
    {"udp", &test_udp},
};

}  // namespace

void select(const Cmdline& cmdline) { g_selected = cmdline.get("ktest"); }

bool needs_drivers() {
  return g_selected == StringView("drivers") || g_selected == StringView("blk") ||
         g_selected == StringView("fs") || g_selected == StringView("fs_check") ||
         g_selected == StringView("fs_crash") || g_selected == StringView("net") ||
         g_selected == StringView("tcp") || g_selected == StringView("sock") ||
         g_selected == StringView("udp") || g_selected == StringView("spawn") ||
         g_selected == StringView("signals") || g_selected == StringView("libc") ||
         g_selected == StringView("sysarea") || g_selected == StringView("sysarea_locked");
}
StringView selected() { return g_selected; }

bool exception_expected(const InterruptFrame& frame) {
  const Expectation& e = g_expect;
  if (!e.active || frame.vector != e.vector) return false;
  if ((frame.error_code & e.error_mask) != e.error_value) {
    kprint("ktest: error code {x} does not match {x}/{x}\n", frame.error_code, e.error_value,
           e.error_mask);
    return false;
  }
  if (e.check_cr2 && arch::x86_64::read_cr2() != e.cr2) {
    kprint("ktest: cr2 {x} != expected {x}\n", arch::x86_64::read_cr2(), e.cr2);
    return false;
  }
  return true;
}

bool panic_expected() { return g_expect_panic; }

void set_initrd(uint64_t phys, uint64_t size) {
  g_initrd_phys = phys;
  g_initrd_size = size;
}

void run_selected() {
  if (g_selected.empty()) return;
  for (const Test& t : kTests) {
    if (t.name == g_selected) {
      kprint("ktest: running {}\n", t.name);
      t.fn();
      fail("test returned without verdict");
    }
  }
  fail("unknown test name");
}

void pass() {
  kprint_unlocked("ktest: PASS {}\n", g_selected);
  arch::x86_64::qemu_exit(arch::x86_64::kExitSuccess);
}

void fail(StringView why) {
  kprint_unlocked("ktest: FAIL {}: {}\n", g_selected, why);
  arch::x86_64::qemu_exit(arch::x86_64::kExitFailure);
}

}  // namespace kernel::ktest
