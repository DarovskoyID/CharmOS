#pragma once
// Менеджер драйверов в ядре (docs/abi/driver.md §1, §7, §8): манифесты из
// initrd, сопоставление с функциями PCI, запуск, перезапуск.

#include <stdint.h>

namespace kernel::devmgr {

// Запускает по одному потоку-надзирателю на каждую пару «устройство — драйвер».
void start(uint64_t initrd_phys, uint64_t initrd_size);

struct DriverStats {
  bool bound;           // для устройства нашёлся драйвер
  bool finished;        // надзиратель сдался (исчерпаны перезапуски)
  uint32_t runs;        // сколько раз драйвер запускался
  int64_t last_exit;    // код последнего завершения
  uint32_t killed;      // сколько раз убит ядром (исключение, kExitKilled)
};

// Статистика драйвера устройства vendor:device (первое совпадение).
DriverStats stats(uint16_t vendor, uint16_t device);

inline constexpr uint32_t kMaxRestarts = 5;
inline constexpr uint64_t kRestartDelayNs = 100'000'000;

}  // namespace kernel::devmgr
