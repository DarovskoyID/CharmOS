#include "base/debug/stack_trace.h"
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <ostream>

namespace base::debug {
namespace {
void PrintBytes(const char* bytes,size_t length) {
  while(length) {
    ssize_t count=write(STDERR_FILENO,bytes,length);
    if(count<0 && errno==EINTR) continue;
    if(count<=0) return;
    bytes+=count; length-=static_cast<size_t>(count);
  }
}
void PrintAddress(const void* address) {
  constexpr char digits[]="0123456789abcdef";
  char text[2+sizeof(uintptr_t)*2+1];
  text[0]='0'; text[1]='x';
  uintptr_t value=reinterpret_cast<uintptr_t>(address);
  for(size_t i=0;i<sizeof(uintptr_t)*2;++i)
    text[2+i]=digits[(value>>((sizeof(uintptr_t)*2-1-i)*4))&15];
  text[sizeof text-1]='\n';
  PrintBytes(text,sizeof text);
}
}

bool EnableInProcessStackDumping() {
  errno=ENOTSUP; // Native fault reporting belongs to the kernel, not sigaction.
  return false;
}
size_t CollectStackTrace(span<const void*> trace) {
  if(trace.empty()) return 0;
  // The native static profile discards unwind tables and may omit frame
  // pointers. Report the actual immediate return address, never walk guessed
  // stack frames or claim that this is a complete unwind.
  trace[0]=__builtin_extract_return_addr(__builtin_return_address(0));
  return trace[0] ? 1u : 0u;
}
#if BUILDFLAG(CAN_UNWIND_WITH_FRAME_POINTERS)
uintptr_t GetStackEnd() { return 0; } // Stack bounds are not exported by libc.
#endif
void StackTrace::PrintMessageWithPrefix(cstring_view prefix,cstring_view message) {
  PrintBytes(prefix.c_str(),prefix.size());
  PrintBytes(message.c_str(),message.size());
}
void StackTrace::PrintWithPrefixImpl(cstring_view prefix) const {
  for(const void* address:addresses()) {
    PrintBytes(prefix.c_str(),prefix.size());
    PrintAddress(address);
  }
}
void StackTrace::OutputToStreamWithPrefixImpl(std::ostream* output,cstring_view prefix) const {
  for(const void* address:addresses()) *output << prefix << address << '\n';
}

namespace internal {
void itoa_r(intptr_t number,int radix,size_t padding,span<char> buffer) {
  if(buffer.empty()) return;
  buffer[0]=0;
  if(radix<2 || radix>16) return;
  bool negative=number<0 && radix==10;
  uintptr_t magnitude=static_cast<uintptr_t>(number);
  if(negative) magnitude=uintptr_t{0}-magnitude;
  char reverse[sizeof(uintptr_t)*8];
  constexpr char digits[]="0123456789abcdef";
  size_t used=0;
  do {
    reverse[used++]=digits[magnitude%static_cast<uintptr_t>(radix)];
    magnitude/=static_cast<uintptr_t>(radix);
  } while(magnitude);
  size_t width=padding>used ? padding : used;
  size_t sign=negative ? 1u : 0u;
  if(width>=buffer.size() || sign>=buffer.size()-width) return;
  size_t position=0;
  if(negative) buffer[position++]='-';
  for(size_t i=used;i<width;++i) buffer[position++]='0';
  while(used) buffer[position++]=reverse[--used];
  buffer[position]=0;
}
}
} // namespace base::debug
