#include "build/build_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <string.h>
#include <vector>

#if !BUILDFLAG(IS_CHARMOS) || !BUILDFLAG(IS_POSIX)
#error Incorrect CharmOS platform flags
#endif
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_ANDROID) || defined(__linux__)
#error Target must not use the Linux userspace ABI
#endif
static_assert(sizeof(void*) == 8 && sizeof(wchar_t) == 4);
extern "C" int main() {
  std::vector<unsigned> values;
  for (unsigned i = 4096; i > 0; --i) values.push_back(i);
  std::sort(values.begin(), values.end());
  for (unsigned i = 0; i < values.size(); ++i) if (values[i] != i + 1) abort();
  auto owned = std::make_unique<unsigned[]>(values.size());
  std::copy(values.begin(), values.end(), owned.get());
  if (owned[4095] != 4096) abort();
  std::atomic<unsigned> counter{7};
  unsigned expected = 7;
  if (!counter.compare_exchange_strong(expected, 11) || counter.fetch_add(5) != 11 || counter.load() != 16) abort();
  const char* text = "CharmOS / Привет / Chromium";
  std::vector<char> unicode(text, text + strlen(text) + 1);
  if (strcmp(unicode.data(), text)) abort();
  printf("platform runtime: CharmOS flags and native GN executable ok\n");
  printf("platform runtime: libc++ vector, sort, ownership, atomics and UTF-8 bytes ok\n");
  return 0;
}
