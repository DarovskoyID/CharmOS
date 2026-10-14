#include "ktest.hpp"

#include <arch/x86_64/cpu.hpp>
#include <arch/x86_64/serial.hpp>

#include "../mm/heap.hpp"
#include "../mm/layout.hpp"
#include "../mm/pmm.hpp"
#include "../mm/vmm.hpp"
#include "../mm/stack.hpp"
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
};

}  // namespace

void select(const Cmdline& cmdline) { g_selected = cmdline.get("ktest"); }
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
