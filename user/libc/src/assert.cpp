#include <assert.h>
#include <charm/sys.hpp>
#include <stdio.h>
#include <stdlib.h>

extern "C" void __charm_assert_fail(const char* expression, const char* file, int line, const char* function) {
  char message[512];
  snprintf(message, sizeof message, "assertion failed: %s (%s:%d, %s)\n", expression, file, line, function);
  charm::print(message);
  abort();
}
