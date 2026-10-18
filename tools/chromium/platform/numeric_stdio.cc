// Optional formatter bridge for the native Chromium runtime. The actual
// floating-point/printf algorithm stays in the licensed upstream dependency.
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>
#include <string.h>
#include "src/stdio/vsnprintf.h"
#include "src/__support/StringUtil/error_to_string.h"

// The upstream default error mapper uses compiler TLS. The native fixed
// messages are already immutable/thread-safe, and use CharmOS errno numbers.
namespace LIBC_NAMESPACE_DECL {
cpp::string_view get_error_string(int number) { return cpp::string_view(::strerror(number)); }
cpp::string_view get_error_string(int number,cpp::span<char>) { return get_error_string(number); }
cpp::optional<cpp::string_view> try_get_errno_name(int number) {
#define ERROR_NAME(name) case name: return cpp::string_view(#name);
  switch(number) {
    ERROR_NAME(EPERM) ERROR_NAME(ENOENT) ERROR_NAME(ESRCH) ERROR_NAME(EINTR)
    ERROR_NAME(EIO) ERROR_NAME(E2BIG) ERROR_NAME(ENOEXEC) ERROR_NAME(EBADF)
    ERROR_NAME(ECHILD) ERROR_NAME(EAGAIN) ERROR_NAME(ENOMEM) ERROR_NAME(EACCES)
    ERROR_NAME(EFAULT) ERROR_NAME(EBUSY) ERROR_NAME(EEXIST) ERROR_NAME(ENOTDIR)
    ERROR_NAME(EISDIR) ERROR_NAME(EINVAL) ERROR_NAME(ENFILE) ERROR_NAME(EMFILE) ERROR_NAME(ENOTTY)
    ERROR_NAME(ENOSPC) ERROR_NAME(ESPIPE) ERROR_NAME(EPIPE) ERROR_NAME(EDOM)
    ERROR_NAME(ERANGE) ERROR_NAME(ENOSYS) ERROR_NAME(ENOTEMPTY) ERROR_NAME(EILSEQ)
    ERROR_NAME(EOVERFLOW) ERROR_NAME(ENOTSOCK) ERROR_NAME(ENOTSUP) ERROR_NAME(EADDRINUSE)
    ERROR_NAME(ENETUNREACH) ERROR_NAME(ECONNRESET) ERROR_NAME(ETIMEDOUT) ERROR_NAME(ECONNREFUSED)
    default: return cpp::nullopt;
  }
#undef ERROR_NAME
}
}

extern "C" int __charm_numeric_vsnprintf(char* out,size_t n,const char* format,va_list args) {
  return LIBC_NAMESPACE::vsnprintf(out,n,format,args);
}

namespace {
// The wide facade currently promises numeric conversions only. Refuse the
// other conversions rather than treating byte widths/counts as wchar widths.
bool numeric_format(const wchar_t* p) {
  for(;*p;++p) {
    if(*p!='%') continue;
    ++p; if(*p=='%') continue;
    while(*p=='-' || *p=='+' || *p==' ' || *p=='#' || *p=='0') ++p;
    if(*p=='*') ++p; else while(*p>='0' && *p<='9') ++p;
    if(*p=='.') { ++p; if(*p=='*') ++p; else while(*p>='0' && *p<='9') ++p; }
    if(*p=='h' || *p=='l') { wchar_t c=*p++; if(*p==c) ++p; }
    else if(*p=='j' || *p=='z' || *p=='t' || *p=='L') ++p;
    switch(*p) {
      case 'd': case 'i': case 'o': case 'u': case 'x': case 'X': case 'p':
      case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': break;
      default: return false;
    }
  }
  return true;
}
}

extern "C" int __charm_numeric_vswprintf(wchar_t* out,size_t n,const wchar_t* format,va_list args) {
  if(!format || (n && !out)) { errno=EINVAL; return -1; }
  if(!numeric_format(format)) { errno=ENOSYS; return -1; }
  if(!n) { errno=ERANGE; return -1; }
  out[0]=0;
  size_t format_size=wcstombs(nullptr,format,0);
  if(format_size==static_cast<size_t>(-1)) return -1;
  auto* narrow_format=static_cast<char*>(malloc(format_size+1));
  if(!narrow_format) return -1;
  if(wcstombs(narrow_format,format,format_size+1)==static_cast<size_t>(-1)) { free(narrow_format); return -1; }
  va_list copy; va_copy(copy,args);
  int count=LIBC_NAMESPACE::vsnprintf(nullptr,0,narrow_format,copy); va_end(copy);
  if(count<0) { free(narrow_format); return -1; }
  auto* bytes=static_cast<char*>(malloc(static_cast<size_t>(count)+1));
  if(!bytes) { free(narrow_format); return -1; }
  va_copy(copy,args);
  int written=LIBC_NAMESPACE::vsnprintf(bytes,static_cast<size_t>(count)+1,narrow_format,copy); va_end(copy);
  free(narrow_format);
  if(written!=count) { free(bytes); errno=EOVERFLOW; return -1; }
  size_t needed=mbstowcs(nullptr,bytes,0);
  if(needed==static_cast<size_t>(-1)) { free(bytes); return -1; }
  if(needed>=n) { free(bytes); errno=ERANGE; return -1; }
  size_t result=mbstowcs(out,bytes,n);
  free(bytes);
  return result==static_cast<size_t>(-1) ? -1 : static_cast<int>(result);
}
