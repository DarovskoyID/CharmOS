#include "ktest.hpp"

#include <arch/x86_64/cpu.hpp>
#include <arch/x86_64/serial.hpp>

#include "../mm/heap.hpp"
#include "../mm/layout.hpp"
#include "../mm/pmm.hpp"
#include "../mm/vmm.hpp"
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
  kprint("ktest: PASS {}\n", g_selected);
  arch::x86_64::qemu_exit(arch::x86_64::kExitSuccess);
}

void fail(StringView why) {
  kprint("ktest: FAIL {}: {}\n", g_selected, why);
  arch::x86_64::qemu_exit(arch::x86_64::kExitFailure);
}

}  // namespace kernel::ktest
