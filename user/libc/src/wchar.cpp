#include <wchar.h>
#include <errno.h>
#include "internal.hpp"

static_assert(sizeof(wchar_t) == 4 && sizeof(mbstate_t) == 12);
static_assert(__builtin_offsetof(mbstate_t, remaining) == 8);
namespace {
size_t invalid(mbstate_t* state) {
  *state = {};
  errno = EILSEQ;
  return static_cast<size_t>(-1);
}
bool scalar(uint32_t c) { return c <= 0x10ffff && !(c >= 0xd800 && c <= 0xdfff); }
}
extern "C" {
size_t wcslen(const wchar_t* s) { size_t n = 0; while (s[n]) ++n; return n; }
size_t wcsnlen(const wchar_t* s, size_t n) { size_t i = 0; while (i < n && s[i]) ++i; return i; }
wchar_t* wmemcpy(wchar_t* d, const wchar_t* s, size_t n) { for (size_t i=0;i<n;++i) d[i]=s[i]; return d; }
wchar_t* wmemmove(wchar_t* d, const wchar_t* s, size_t n) {
  if (reinterpret_cast<uintptr_t>(d) < reinterpret_cast<uintptr_t>(s)) return wmemcpy(d,s,n);
  while (n) { --n; d[n]=s[n]; } return d;
}
wchar_t* wmemset(wchar_t* d, wchar_t c, size_t n) { for(size_t i=0;i<n;++i) d[i]=c; return d; }
int wmemcmp(const wchar_t* a, const wchar_t* b, size_t n) {
  for(size_t i=0;i<n;++i) if(a[i]!=b[i]) return a[i]<b[i]?-1:1;
  return 0;
}
wchar_t* wmemchr(const wchar_t* s, wchar_t c, size_t n) {
  for(size_t i=0;i<n;++i) if(s[i]==c) return const_cast<wchar_t*>(s+i);
  return nullptr;
}
wchar_t* wcscpy(wchar_t* d, const wchar_t* s) { return wmemcpy(d,s,wcslen(s)+1); }
wchar_t* wcsncpy(wchar_t* d, const wchar_t* s, size_t n) {
  size_t i=0; while(i<n && s[i]) { d[i]=s[i]; ++i; }
  while(i<n) d[i++]=0; return d;
}
wchar_t* wcscat(wchar_t* d, const wchar_t* s) { wcscpy(d+wcslen(d),s); return d; }
wchar_t* wcsncat(wchar_t* d, const wchar_t* s, size_t n) {
  size_t end=wcslen(d), i=0; while(i<n && s[i]) { d[end+i]=s[i]; ++i; }
  d[end+i]=0; return d;
}
int wcscmp(const wchar_t* a, const wchar_t* b) {
  while(*a && *a==*b) { ++a; ++b; } return *a==*b?0:(*a<*b?-1:1);
}
int wcsncmp(const wchar_t* a, const wchar_t* b, size_t n) {
  for(size_t i=0;i<n;++i) { if(a[i]!=b[i]) return a[i]<b[i]?-1:1; if(!a[i]) break; }
  return 0;
}
wchar_t* wcschr(const wchar_t* s, wchar_t c) {
  do { if(*s==c) return const_cast<wchar_t*>(s); } while(*s++); return nullptr;
}
wchar_t* wcsrchr(const wchar_t* s, wchar_t c) {
  const wchar_t* found=nullptr; do { if(*s==c) found=s; } while(*s++); return const_cast<wchar_t*>(found);
}
wchar_t* wcsstr(const wchar_t* s, const wchar_t* needle) {
  if(!*needle) return const_cast<wchar_t*>(s);
  size_t n=wcslen(needle);
  for(;*s;++s) if(*s==*needle && wcsncmp(s,needle,n)==0) return const_cast<wchar_t*>(s);
  return nullptr;
}
wchar_t* wcspbrk(const wchar_t* s, const wchar_t* accept) {
  for(;*s;++s) if(wcschr(accept,*s)) return const_cast<wchar_t*>(s); return nullptr;
}
size_t wcsspn(const wchar_t* s, const wchar_t* accept) { size_t n=0; while(s[n] && wcschr(accept,s[n])) ++n; return n; }
size_t wcscspn(const wchar_t* s, const wchar_t* reject) { size_t n=0; while(s[n] && !wcschr(reject,s[n])) ++n; return n; }
wchar_t* wcstok(wchar_t* s, const wchar_t* delim, wchar_t** save) {
  if(!s) s=*save; if(!s) return nullptr; s+=wcsspn(s,delim);
  if(!*s) { *save=nullptr; return nullptr; }
  wchar_t* token=s; s+=wcscspn(s,delim); if(*s) { *s=0; *save=s+1; } else *save=nullptr;
  return token;
}
int wcscoll(const wchar_t* a, const wchar_t* b) { return wcscmp(a,b); }
size_t wcsxfrm(wchar_t* out, const wchar_t* s, size_t n) { size_t size=wcslen(s); if(n) wcsncpy(out,s,n); return size; }
int mbsinit(const mbstate_t* s) { return !s || s->remaining==0; }
size_t mbrtowc(wchar_t* out, const char* text, size_t n, mbstate_t* supplied) {
  auto* state=supplied?supplied:&libc::current_thread()->mb_decode;
  if(!text) { text=""; n=1; out=nullptr; }
  if(state->remaining>3 || (state->remaining && (state->total<2 || state->total>4 || state->remaining>=state->total)))
    return invalid(state);
  size_t used=0;
  if(!state->remaining) {
    if(!n) return static_cast<size_t>(-2);
    uint8_t c=static_cast<uint8_t>(text[used++]);
    if(c<0x80) { *state={}; if(out) *out=c; return c?1:0; }
    if(c>=0xc2 && c<=0xdf) *state={static_cast<uint32_t>(c&0x1f),0x80,1,2,0};
    else if(c>=0xe0 && c<=0xef) *state={static_cast<uint32_t>(c&0x0f),0x800,2,3,0};
    else if(c>=0xf0 && c<=0xf4) *state={static_cast<uint32_t>(c&7),0x10000,3,4,0};
    else return invalid(state);
  }
  while(state->remaining) {
    if(used==n) return static_cast<size_t>(-2);
    uint8_t c=static_cast<uint8_t>(text[used++]);
    if((c&0xc0)!=0x80) return invalid(state);
    if(state->remaining==state->total-1 &&
       ((state->total==3 && ((state->value==0 && c<0xa0) || (state->value==13 && c>=0xa0))) ||
        (state->total==4 && ((state->value==0 && c<0x90) || (state->value==4 && c>=0x90))))) return invalid(state);
    state->value=(state->value<<6)|(c&0x3f); --state->remaining;
  }
  uint32_t value=state->value;
  if(value<state->minimum || !scalar(value)) return invalid(state);
  *state={}; if(out) *out=static_cast<wchar_t>(value); return used;
}
size_t mbrlen(const char* s, size_t n, mbstate_t* state) {
  return mbrtowc(nullptr,s,n,state?state:&libc::current_thread()->mb_length);
}
size_t wcrtomb(char* out, wchar_t wc, mbstate_t* supplied) {
  auto* state=supplied?supplied:&libc::current_thread()->mb_encode;
  *state={}; if(!out) return 1;
  uint32_t c=static_cast<uint32_t>(wc);
  if(!scalar(c)) return invalid(state);
  if(c<0x80) { out[0]=static_cast<char>(c); return 1; }
  size_t n=c<0x800?2:(c<0x10000?3:4);
  for(size_t i=n-1;i>0;--i) { out[i]=static_cast<char>(0x80|(c&0x3f)); c>>=6; }
  out[0]=static_cast<char>((n==2?0xc0:(n==3?0xe0:0xf0))|c); return n;
}
size_t mbsrtowcs(wchar_t* out, const char** source, size_t n, mbstate_t* supplied) {
  auto* original=supplied?supplied:&libc::current_thread()->mb_string;
  mbstate_t local=*original; auto* state=out?original:&local;
  const char* s=*source; size_t count=0;
  while(!out || count<n) {
    wchar_t wc; size_t used=mbrtowc(&wc,s,4,state);
    if(used==static_cast<size_t>(-1) || used==static_cast<size_t>(-2)) {
      if(out) *source=s; return invalid(state);
    }
    if(!used) { if(out) { out[count]=0; *source=nullptr; } return count; }
    if(out) out[count]=wc; ++count; s+=used;
  }
  *source=s; return count;
}
size_t wcsrtombs(char* out, const wchar_t** source, size_t n, mbstate_t* supplied) {
  auto* original=supplied?supplied:&libc::current_thread()->mb_encode;
  mbstate_t local=*original; auto* state=out?original:&local;
  const wchar_t* s=*source; size_t count=0;
  while(!out || count<n) {
    char bytes[4]; size_t used=wcrtomb(bytes,*s,state);
    if(used==static_cast<size_t>(-1)) { if(out) *source=s; return used; }
    if(out && used>n-count) break;
    if(!*s) { if(out) { out[count]=0; *source=nullptr; } return count; }
    if(out) for(size_t i=0;i<used;++i) out[count+i]=bytes[i];
    count+=used; ++s;
  }
  *source=s; return count;
}
wint_t btowc(int c) { return c>=0 && c<128?static_cast<wint_t>(c):WEOF; }
int wctob(wint_t c) { return c<128?static_cast<int>(c):EOF; }
}
