// fscanf support lives in its own object: only programs that actually use it
// pull the vsscanf dependency (provided by the LLVM numeric stdio engine).
#include <stdarg.h>
#include <stdio.h>

extern "C" int vfscanf(FILE* f, const char* format, va_list ap) {
  char line[1024];
  if (fgets(line, sizeof line, f) == nullptr) return EOF;
  return vsscanf(line, format, ap);
}

extern "C" int fscanf(FILE* f, const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  int result = vfscanf(f, format, ap);
  va_end(ap);
  return result;
}
