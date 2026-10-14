// Точка входа ядра на C++. Порядок инициализации важен: каждая следующая
// подсистема опирается на предыдущие.

#include <abi/boot.hpp>
#include <arch/x86_64/cpu.hpp>
#include <arch/x86_64/idt.hpp>
#include <arch/x86_64/serial.hpp>

#include "../mm/heap.hpp"
#include "../mm/layout.hpp"
#include "../mm/pmm.hpp"
#include "../mm/vmm.hpp"
#include "../drivers/devmgr.hpp"
#include "../pci/pci.hpp"
#include "../platform/platform.hpp"
#include "../proc/uaccess.hpp"
#include "../sched/sched.hpp"
#include "../smp/smp.hpp"
#include "boot_info.hpp"
#include "cmdline.hpp"
#include "ktest.hpp"
#include "log.hpp"
#include "panic.hpp"

extern "C" [[noreturn]] void kernel_main(uint64_t boot_info_phys, uint64_t stack_aligned);

namespace {

using abi::boot::BootInfo;
using abi::boot::MemoryRegion;
using abi::boot::MemoryType;
using kernel::kprint;
using kernel::panic;
using kernel::mm::phys_to_virt;

// Переменная в .bss: загрузчик обязан обнулить хвост сегмента.
constinit uint64_t g_bss_probe[64];

// Проверка контракта docs/abi/boot.md (то, что тестировал M1).
void verify_handoff(const BootInfo& info, uint64_t stack_aligned) {
  if (const char* err = kernel::check_boot_info(info)) {
    kprint("  magic={X} (want {X}) version={}.{} size={}\n", info.magic, abi::boot::kMagic,
           info.version_major, info.version_minor, info.size);
    panic("boot ABI: {}", err);
  }
  kprint("boot: BootInfo v{}.{}\n", info.version_major, info.version_minor);

  using namespace arch::x86_64;
  if (stack_aligned == 0) panic("boot ABI: stack not aligned");
  if ((read_rflags() & kRflagsIf) != 0) panic("boot ABI: interrupts enabled at entry");
  if ((read_rflags() & kRflagsDf) != 0) panic("boot ABI: direction flag set at entry");
  if ((read_cr0() & kCr0Wp) == 0) panic("boot ABI: CR0.WP not set");
  if ((rdmsr(kMsrEfer) & kEferNxe) == 0) panic("boot ABI: EFER.NXE not set");
  for (uint64_t v : g_bss_probe) {
    if (v != 0) panic("boot ABI: .bss not zeroed");
  }

  const auto* regions = phys_to_virt<const MemoryRegion>(info.memory_map);
  uint64_t usable = 0;
  for (uint64_t i = 0; i < info.memory_map_count; ++i) {
    const MemoryRegion& r = regions[i];
    if (i > 0 && regions[i - 1].base + regions[i - 1].length > r.base) {
      panic("boot ABI: memory map unsorted or overlapping");
    }
    if (r.base % 4096 != 0 || r.length % 4096 != 0) panic("boot ABI: region not page aligned");
    if (r.type == MemoryType::Usable) usable += r.length;
  }
  kprint("boot: kernel_main={X}\n", reinterpret_cast<uint64_t>(&kernel_main));
  kprint("boot: memory regions={} usable MiB={}\n", info.memory_map_count, usable >> 20);

  const abi::boot::Framebuffer& fb = info.framebuffer;
  if (fb.base != 0) {
    kprint("boot: framebuffer {}x{}\n", fb.width, fb.height);
  } else {
    kprint("boot: no framebuffer\n");
  }

  if (info.acpi_rsdp == 0) panic("boot ABI: no RSDP");
  const auto* rsdp = phys_to_virt<const char>(info.acpi_rsdp);
  if (lib::core::StringView(rsdp, 8) != lib::core::StringView("RSD PTR ")) {
    panic("boot ABI: RSDP signature mismatch");
  }
  kprint("boot: rsdp={X}\n", info.acpi_rsdp);

  if (info.cmdline != 0) {
    kprint("boot: cmdline={}\n",
           lib::core::StringView(phys_to_virt<const char>(info.cmdline), info.cmdline_length));
  } else {
    kprint("boot: no cmdline\n");
  }
  kprint("boot: handoff verified\n");
}

void page_fault_handler(arch::x86_64::InterruptFrame& frame) {
  if (kernel::proc::fixup_uaccess_fault(frame)) return;
  kernel::unhandled_interrupt(frame);
}

constinit uint64_t g_initrd_phys = 0;
constinit uint64_t g_initrd_size = 0;

// Первый поток ядра: всё, что идёт после запуска планировщика.
void kmain(void*) {
  // Тесты ядра работают с устройствами сами; драйверы — только в обычной
  // загрузке и в тесте драйверов.
  if (kernel::ktest::selected().empty() ||
      kernel::ktest::selected() == lib::core::StringView("drivers")) {
    kernel::devmgr::start(g_initrd_phys, g_initrd_size);
  }
  kernel::ktest::run_selected();

  uint64_t start = kernel::platform::ticks();
  kernel::sched::sleep_ns(100'000'000);
  kprint("timer: {} ticks in 100 ms on cpu 0, {} CPUs scheduling\n",
         kernel::platform::ticks() - start, kernel::smp::cpu_count());

  kprint("CharmOS kernel: init done\n");
  arch::x86_64::qemu_exit(arch::x86_64::kExitSuccess);
}

}  // namespace

extern "C" [[noreturn]] void kernel_main(uint64_t boot_info_phys, uint64_t stack_aligned) {
  kprint("CharmOS kernel: entry\n");
  const auto& info = *phys_to_virt<const BootInfo>(boot_info_phys);

  // Исключения ловим как можно раньше, чтобы любая ошибка дальше давала дамп.
  kernel::smp::early_init_bsp();
  arch::x86_64::set_fallback_handler(&kernel::unhandled_interrupt);
  arch::x86_64::set_interrupt_handler(arch::x86_64::kVectorPageFault, &page_fault_handler);
  kprint("cpu: GDT, TSS and IDT loaded\n");

  verify_handoff(info, stack_aligned);

  kernel::Cmdline cmdline;
  if (info.cmdline != 0) {
    cmdline = kernel::Cmdline(
        lib::core::StringView(phys_to_virt<const char>(info.cmdline), info.cmdline_length));
  }
  kernel::ktest::select(cmdline);
  kernel::ktest::set_initrd(info.initrd_base, info.initrd_size);
  g_initrd_phys = info.initrd_base;
  g_initrd_size = info.initrd_size;

  kernel::mm::pmm_init(info);
  kernel::mm::vmm_init(info);
  kernel::mm::heap_init();
  kernel::platform::platform_init(info, cmdline);
  kernel::pci::pci_init(info);
  kernel::smp::late_init_bsp();
  kernel::smp::start_aps();

  kernel::sched::init_this_cpu(kernel::sched::kTickHz);
  if (kernel::sched::spawn("kmain", &kmain, nullptr, 0).is_err()) panic("cannot start kmain");
  kernel::sched::idle_loop();
}
