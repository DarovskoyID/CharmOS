// Точка входа ядра на C++. Порядок инициализации важен: каждая следующая
// подсистема опирается на предыдущие.

#include <abi/boot.hpp>
#include <abi/process.hpp>
#include <arch/x86_64/cpu.hpp>
#include <arch/x86_64/idt.hpp>
#include <arch/x86_64/serial.hpp>

#include "../mm/heap.hpp"
#include "../mm/layout.hpp"
#include "../mm/pmm.hpp"
#include "../mm/vmm.hpp"
#include "../console/console.hpp"
#include "../drivers/devmgr.hpp"
#include "../fs/vfs.hpp"
#include "../ipc/ipc.hpp"
#include "../net/netif.hpp"
#include "../net/socket.hpp"
#include "../net/udp.hpp"
#include "../pci/iommu.hpp"
#include "../pci/pci.hpp"
#include "../platform/platform.hpp"
#include "../proc/process.hpp"
#include "../proc/uaccess.hpp"
#include "../sched/sched.hpp"
#include "../smp/smp.hpp"
#include <lib/initrd/initrd.hpp>

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
constinit char g_root_spec[32] = {};
constinit size_t g_root_len = 0;
constinit uint64_t g_initrd_size = 0;
constinit char g_init_spec[64] = {};
constinit size_t g_init_len = 0;

// init (M8): программа из initrd (init=bin/init) со стартовым сообщением
// "strt" (docs/abi/process.md §2): роли Root, Console, Net, Job (корневой).
// Её завершение — завершение системы (выключения через ACPI ещё нет, M9):
// код 0 — успешный выход QEMU.
[[noreturn]] void run_init() {
  namespace ap = abi::process;
  using kernel::ipc::Cap;
  lib::core::StringView path(g_init_spec, g_init_len);
  lib::initrd::Archive archive;
  lib::initrd::File f;
  if (archive.open(phys_to_virt<const void>(g_initrd_phys), g_initrd_size).is_err() || !archive.find(path, &f)) {
    kprint("init: {} not found in initrd\n", path);
    arch::x86_64::qemu_exit(arch::x86_64::kExitFailure);
  }
  // Сеть: дождаться линка и адреса, если сетевой драйвер есть.
  uint64_t deadline = kernel::platform::now_ns() + 5'000'000'000ull;
  while (!kernel::net::link_up() && kernel::platform::now_ns() < deadline) kernel::sched::sleep_ns(10'000'000);
  if (kernel::net::link_up()) {
    auto s = kernel::net::dhcp_configure(kernel::platform::now_ns() + 10'000'000'000ull);
    if (s != kernel::net::Status::Ok) kprint("init: no DHCP lease\n");
  }
  Cap* caps[4];
  uint32_t roles[4];
  uint32_t n = 0;
  if (kernel::fs::mounted() && kernel::fs::root_cap(&caps[n]) == kernel::ipc::Status::Ok) roles[n++] = ap::kRoleRoot;
  if (kernel::console::console_cap(&caps[n]) == kernel::ipc::Status::Ok) roles[n++] = ap::kRoleConsole;
  if (kernel::net::net_cap(&caps[n]) == kernel::ipc::Status::Ok) roles[n++] = ap::kRoleNet;
  if (kernel::ipc::create_job_cap(kernel::ipc::root_job(), &caps[n]) == kernel::ipc::Status::Ok) roles[n++] = ap::kRoleJob;
  struct {
    ap::StartHeader h;
    char data[32];
  } msg{};
  static const char kArgs[] = "init";
  static const char kEnv[] = "PATH=/bin";
  __builtin_memcpy(msg.h.magic, ap::kStartMagic, 4);
  msg.h.argc = 1;
  msg.h.args_bytes = sizeof kArgs;
  msg.h.envc = 1;
  msg.h.env_bytes = sizeof kEnv;
  msg.h.handle_count = n;
  for (uint32_t i = 0; i < n; ++i) msg.h.roles[i] = roles[i];
  __builtin_memcpy(msg.data, kArgs, sizeof kArgs);
  __builtin_memcpy(msg.data + sizeof kArgs, kEnv, sizeof kEnv);
  Cap *mine, *theirs;
  if (kernel::ipc::create_channel(&mine, &theirs) != kernel::ipc::Status::Ok ||
      kernel::ipc::kernel_send(mine, &msg, sizeof msg.h + sizeof kArgs + sizeof kEnv, caps, n) !=
          kernel::ipc::Status::Ok) {
    panic("init: cannot build the start message");
  }
  auto p = kernel::proc::spawn_process(f.name.substr(4), f.data, f.size, kernel::sched::kAnyCpu, theirs);
  if (p.is_err()) panic("init: cannot start {}", path);
  kprint("init: started {}\n", path);
  int64_t code = kernel::proc::wait_process(p.value());
  kernel::ipc::close_detached(mine);
  kprint("init: exited with {}, halting\n", code);
  (void)kernel::fs::sync_all();
  arch::x86_64::qemu_exit(code == 0 ? arch::x86_64::kExitSuccess : arch::x86_64::kExitFailure);
}

// Первый поток ядра: всё, что идёт после запуска планировщика.
void kmain(void*) {
  // Тесты ядра работают с устройствами сами; драйверы — только в обычной
  // загрузке и в тесте драйверов.
  if (kernel::ktest::selected().empty() || kernel::ktest::needs_drivers()) {
    kernel::devmgr::start(g_initrd_phys, g_initrd_size);
    if (g_root_len != 0) (void)kernel::fs::mount_root(lib::core::StringView(g_root_spec, g_root_len));
  }
  kernel::ktest::run_selected();
  if (kernel::ktest::selected().empty() && g_init_len != 0) run_init();

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
  if (auto root = cmdline.get("root"); !root.empty() && root.size() < sizeof g_root_spec) {
    for (size_t i = 0; i < root.size(); ++i) g_root_spec[i] = root[i];
    g_root_len = root.size();
  }
  if (auto init = cmdline.get("init"); !init.empty() && init.size() < sizeof g_init_spec) {
    for (size_t i = 0; i < init.size(); ++i) g_init_spec[i] = init[i];
    g_init_len = init.size();
  }
  kernel::ktest::set_initrd(info.initrd_base, info.initrd_size);
  g_initrd_phys = info.initrd_base;
  g_initrd_size = info.initrd_size;

  kernel::mm::pmm_init(info);
  kernel::mm::vmm_init(info);
  kernel::mm::heap_init();
  kernel::platform::platform_init(info, cmdline);
  kernel::pci::pci_init(info);
  kernel::pci::iommu::init(info, cmdline);
  kernel::console::init();
  kernel::console::set_control_handler(&kernel::proc::signal_foreground);
  kernel::proc::init_accounting();
  kernel::smp::late_init_bsp();
  kernel::smp::start_aps();

  kernel::sched::init_this_cpu(kernel::sched::kTickHz);
  if (kernel::sched::spawn("kmain", &kmain, nullptr, 0).is_err()) panic("cannot start kmain");
  kernel::sched::idle_loop();
}
