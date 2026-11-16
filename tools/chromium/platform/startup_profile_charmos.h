#pragma once
#include "build/build_config.h"
#if BUILDFLAG(IS_CHARMOS)
#include <stdio.h>
#include <time.h>
#include <unistd.h>
namespace charmos_startup_profile {
struct Entry { const char* name; unsigned long long wall_us, cpu_us; };
inline Entry entries[64] = {};
inline unsigned count = 0;
inline bool dumped = false;
inline unsigned long long Micros(const timespec& time) {
  return static_cast<unsigned long long>(time.tv_sec) * 1000000 + time.tv_nsec / 1000;
}
}
inline void CharmStartupStage(const char* stage) {
  using namespace charmos_startup_profile;
  timespec wall{}, cpu{};
  if (clock_gettime(CLOCK_MONOTONIC, &wall) != 0) return;
  (void)clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu);
  unsigned slot = __atomic_fetch_add(&count, 1u, __ATOMIC_RELAXED);
  if (slot < 64) {
    entries[slot].wall_us = Micros(wall);
    entries[slot].cpu_us = Micros(cpu);
    __atomic_store_n(&entries[slot].name, stage, __ATOMIC_RELEASE);
  }
}
// Serial output is synchronous and can distort startup. Emit the captured
// timestamps only after the compositor acknowledges the first displayed frame.
inline void CharmStartupDump() {
  using namespace charmos_startup_profile;
  if (__atomic_exchange_n(&dumped, true, __ATOMIC_RELAXED)) return;
  unsigned size = __atomic_load_n(&count, __ATOMIC_RELAXED);
  for (unsigned slot = 0; slot < size && slot < 64; ++slot) {
    const char* name = __atomic_load_n(&entries[slot].name, __ATOMIC_ACQUIRE);
    if (!name) continue;
    char line[192];
    int length = snprintf(line, sizeof line, "browser: stage %s %llu us cpu %llu us\n",
                          name, entries[slot].wall_us, entries[slot].cpu_us);
    if (length > 0 && static_cast<unsigned>(length) < sizeof line)
      (void)write(STDERR_FILENO, line, static_cast<unsigned>(length));
  }
}
#else
inline void CharmStartupStage(const char*) {}
inline void CharmStartupDump() {}
#endif
