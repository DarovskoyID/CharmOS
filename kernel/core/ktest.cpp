#include "ktest.hpp"

#include <arch/x86_64/cpu.hpp>
#include <arch/x86_64/serial.hpp>

#include "../mm/heap.hpp"
#include "../mm/layout.hpp"
#include "../mm/pmm.hpp"
#include "../mm/vmm.hpp"
#include "../mm/stack.hpp"
#include "../platform/platform.hpp"
#include "../smp/smp.hpp"
#include "../sync/spinlock.hpp"
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

  // Куча: случайные выделения с проверкой содержимого.
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
  if (!mm::heap_check() || mm::heap_used_bytes() != 0) fail("heap: inconsistent after stress");
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
  // 100 Гц, а не 1000: под QEMU без KVM (CI) эмулятор не успевает доставить
  // каждое прерывание с периодом 1 мс, и пропущенные периодические тики
  // теряются — это проверяло бы скорость эмулятора, а не ядро.
  constexpr uint32_t kHz = 100;
  constexpr uint64_t kWindowNs = 500'000'000;
  platform::start_tick(kHz);
  const auto& hpet = platform::hpet();
  uint64_t h0 = hpet.counter();
  uint64_t tsc0 = arch::x86_64::rdtsc();
  uint64_t t0 = platform::ticks();
  while (hpet.elapsed(h0, hpet.counter()) < hpet.ns_to_ticks(kWindowNs)) {
    asm volatile("hlt");  // будит прерывание таймера
  }
  uint64_t t1 = platform::ticks();
  uint64_t tsc1 = arch::x86_64::rdtsc();
  uint64_t ns = hpet.ticks_to_ns(hpet.elapsed(h0, hpet.counter()));
  arch::x86_64::cli();
  platform::local_apic().timer_stop();

  uint64_t ticks = t1 - t0;
  uint64_t measured_hz = ticks * 1'000'000'000 / ns;
  uint64_t tsc_ns = (tsc1 - tsc0) * 1'000'000'000 / platform::tsc_hz();
  kprint("ktest: {} ticks in {} us -> {} Hz; TSC says {} us\n", ticks, ns / 1000, measured_hz,
         tsc_ns / 1000);
  // Допуск 10%: в эмуляторе таймер может немного «плыть».
  if (measured_hz < kHz * 9 / 10 || measured_hz > kHz * 11 / 10) fail("tick rate off by >10%");
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
constexpr uint64_t kIncrements = 200'000;

void count_under_lock(void*) {
  uint32_t me = smp::this_cpu().index;
  for (uint64_t i = 0; i < kIncrements; ++i) {
    sync::SpinGuard guard(g_counter.lock);
    // Неатомарное чтение-изменение-запись: без блокировки результат был бы меньше.
    g_counter.counter = g_counter.counter + 1;
  }
  g_counter.per_cpu[me] = kIncrements;
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
  smp::run_on_all_cpus(&count_under_lock, nullptr);
  for (uint32_t i = 0; i < n; ++i) {
    if (g_counter.per_cpu[i] != kIncrements) fail("a CPU did not run the work");
  }
  kprint("ktest: {} CPUs x {} locked increments = {}\n", n, kIncrements, g_counter.counter);
  if (g_counter.counter != n * kIncrements) fail("lost updates under spinlock");
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
