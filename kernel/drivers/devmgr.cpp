#include "devmgr.hpp"

#include <lib/core/fmt.hpp>
#include <lib/initrd/initrd.hpp>
#include <lib/manifest/manifest.hpp>

#include "../core/log.hpp"
#include "../ipc/ipc.hpp"
#include "../mm/heap.hpp"
#include "../mm/layout.hpp"
#include "../pci/pci.hpp"
#include "../proc/process.hpp"
#include "../sched/sched.hpp"

namespace kernel::devmgr {

using lib::core::StringView;

namespace {

constexpr uint32_t kMaxDrivers = 32;

struct Driver {
  lib::manifest::Manifest manifest;
  lib::initrd::File elf;
};

struct Binding {
  uint32_t pci_index;
  const Driver* driver;
  volatile DriverStats stats;
};

constinit Driver g_drivers[kMaxDrivers] = {};
constinit uint32_t g_driver_count = 0;
constinit Binding g_bindings[pci::kMaxFunctions] = {};
constinit uint32_t g_binding_count = 0;

bool ends_with(StringView s, StringView suffix) {
  return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

void load_manifests(const lib::initrd::Archive& archive) {
  for (uint32_t i = 0; i < archive.count(); ++i) {
    lib::initrd::File f = archive.file(i);
    if (!f.name.starts_with("drivers/") || !ends_with(f.name, ".toml")) continue;
    auto m = lib::manifest::parse(StringView(reinterpret_cast<const char*>(f.data), f.size));
    if (m.is_err()) {
      kprint("devmgr: {}: line {}: {}\n", f.name, m.error().line, m.error().message);
      continue;
    }
    // Имя в манифесте совпадает с именем файла (docs/abi/driver.md §7).
    StringView base = f.name.substr(8, f.name.size() - 8 - 5);
    if (StringView(m.value().name) != base) {
      kprint("devmgr: {}: name does not match file name\n", f.name);
      continue;
    }
    char elf_name[64];
    StringView elf_path = lib::core::format_to(elf_name, sizeof elf_name, "drivers/{}.elf", base);
    lib::initrd::File elf;
    if (!archive.find(elf_path, &elf)) {
      kprint("devmgr: {}: no {}\n", f.name, elf_path);
      continue;
    }
    if (g_driver_count == kMaxDrivers) break;
    g_drivers[g_driver_count++] = {m.value(), elf};
  }
}

bool name_less(const char* a, const char* b) {
  while (*a != '\0' && *a == *b) {
    ++a;
    ++b;
  }
  return static_cast<unsigned char>(*a) < static_cast<unsigned char>(*b);
}

const Driver* best_driver(const pci::Function& f) {
  const Driver* best = nullptr;
  int64_t best_score = -1;
  lib::manifest::DeviceId id{f.vendor, f.device_id, f.class_code};
  for (uint32_t i = 0; i < g_driver_count; ++i) {
    int64_t s = lib::manifest::match_score(g_drivers[i].manifest, id);
    if (s < 0) continue;
    // Больший score; при равенстве — по имени (детерминированно).
    if (s > best_score || (s == best_score && name_less(g_drivers[i].manifest.name,
                                                        best->manifest.name))) {
      best = &g_drivers[i];
      best_score = s;
    }
  }
  return best;
}

void supervise(void* arg) {
  auto* b = static_cast<Binding*>(arg);
  const pci::Function& f = pci::function(b->pci_index);
  const char* name = b->driver->manifest.name;
  for (uint32_t attempt = 0;; ++attempt) {
    ipc::Cap* dev = nullptr;
    ipc::Cap* kernel_end = nullptr;
    ipc::Cap* driver_end = nullptr;
    if (ipc::create_device(b->pci_index, &dev) != ipc::Status::Ok ||
        ipc::create_channel(&kernel_end, &driver_end) != ipc::Status::Ok ||
        ipc::kernel_send(kernel_end, "device", 6, &dev, 1) != ipc::Status::Ok) {
      kprint("devmgr: {}: out of memory starting driver\n", name);
      break;
    }
    auto p = proc::spawn_process(name, b->driver->elf.data, b->driver->elf.size, sched::kAnyCpu,
                                 driver_end);
    if (p.is_err()) {
      kprint("devmgr: {}: cannot start driver\n", name);
      ipc::release_detached(driver_end);
      ipc::release_detached(kernel_end);
      break;
    }
    b->stats.runs = b->stats.runs + 1;
    int64_t code = proc::wait_process(p.value());
    ipc::release_detached(kernel_end);
    b->stats.last_exit = code;
    if (code == proc::kExitKilled) b->stats.killed = b->stats.killed + 1;
    kprint("devmgr: {} on {}:{}.{} exited with {} (run {})\n", name, f.bus, f.device, f.function,
           code, b->stats.runs);
    if (attempt == kMaxRestarts) {
      kprint("devmgr: {} on {}:{}.{} failed {} times, giving up\n", name, f.bus, f.device,
             f.function, b->stats.runs);
      break;
    }
    sched::sleep_ns(kRestartDelayNs);
  }
  b->stats.finished = true;
}

}  // namespace

void start(uint64_t initrd_phys, uint64_t initrd_size) {
  if (initrd_phys == 0) {
    kprint("devmgr: no initrd, no drivers\n");
    return;
  }
  lib::initrd::Archive archive;
  if (archive.open(mm::phys_to_virt<const void>(initrd_phys), initrd_size).is_err()) {
    kprint("devmgr: bad initrd\n");
    return;
  }
  load_manifests(archive);
  kprint("devmgr: {} driver manifests\n", g_driver_count);
  for (uint32_t i = 0; i < pci::function_count(); ++i) {
    const Driver* d = best_driver(pci::function(i));
    if (d == nullptr) continue;
    Binding& b = g_bindings[g_binding_count++];
    b.pci_index = i;
    b.driver = d;
    b.stats.bound = true;
    const pci::Function& f = pci::function(i);
    kprint("devmgr: {}:{}.{} {x}:{x} -> {}\n", f.bus, f.device, f.function, f.vendor, f.device_id,
           d->manifest.name);
    if (sched::spawn("devmgr", &supervise, &b).is_err()) {
      kprint("devmgr: cannot start supervisor\n");
    }
  }
}

DriverStats stats(uint16_t vendor, uint16_t device) {
  for (uint32_t i = 0; i < g_binding_count; ++i) {
    const pci::Function& f = pci::function(g_bindings[i].pci_index);
    if (f.vendor == vendor && f.device_id == device) {
      const volatile DriverStats& s = g_bindings[i].stats;
      return {s.bound, s.finished, s.runs, s.last_exit, s.killed};
    }
  }
  return {};
}

}  // namespace kernel::devmgr
